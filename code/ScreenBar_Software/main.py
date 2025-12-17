#!/usr/bin/env python
# -*- coding: utf-8 -*-
# @Author   : 漫游slvolf https://space.bilibili.com/1054896810
# @File     : main.py
# @Project  : ScreenBar_Software
import sys
import socket
import json
import configparser
import os
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QLineEdit, QPushButton, QFrame, QMessageBox,
    QDialog, QDialogButtonBox, QFormLayout
)
from PySide6.QtCore import Signal, QObject, Qt, QThread
from PySide6.QtGui import QFont, QColor


# -------------------------- 基础配置 --------------------------
def get_config_path():
    if getattr(sys, 'frozen', False):
        return os.path.join(os.path.dirname(sys.executable), "config.ini")
    return "config.ini"


CONFIG_PATH = get_config_path()
config = configparser.ConfigParser()


# -------------------------- 信号类 --------------------------
class Communicate(QObject):
    update_status = Signal(dict)
    update_log = Signal(str, str)


# -------------------------- 网络线程（修复stop方法） --------------------------
class NetworkThread(QThread):
    def __init__(self, host, port):
        super().__init__()
        self.host = host
        self.port = port
        self.running = True
        self.client_socket = None
        self.recv_buffer = ""
        self.comm = Communicate()
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.server_socket.bind((self.host, self.port))
            self.server_socket.listen(1)
            self.server_socket.settimeout(1.0)
            self.comm.update_log.emit(f"服务端启动: {host}:{port}", "blue")
        except Exception as e:
            self.comm.update_log.emit(f"服务端启动失败: {str(e)}", "red")

    def run(self):
        while self.running:
            if not self.client_socket:
                try:
                    self.client_socket, addr = self.server_socket.accept()
                    self.comm.update_log.emit(f"ESP8266已连接: {addr}", "green")
                    self.client_socket.settimeout(1.0)
                except socket.timeout:
                    continue
                except Exception as e:
                    self.comm.update_log.emit(f"连接异常: {str(e)}", "red")
                    continue
            try:
                chunk = self.client_socket.recv(256)
                if not chunk:
                    continue
                try:
                    text = chunk.decode('utf-8', errors='ignore')
                except Exception:
                    text = ''
                if text:
                    self.recv_buffer += text
                    # 逐行处理，以'\n'为结束符，兼容'\r\n'
                    while '\n' in self.recv_buffer:
                        line, self.recv_buffer = self.recv_buffer.split('\n', 1)
                        line = line.strip('\r')
                        if line:
                            self.parse_data(line)
            except socket.timeout:
                continue
            except (ConnectionResetError, BrokenPipeError):
                self.client_socket.close()
                self.client_socket = None
                self.comm.update_log.emit("ESP8266断开，等待重连...", "red")

    def parse_data(self, data):
        try:
            obj = json.loads(data)
            status = {}
            if "pwm" in obj:
                status["warm"] = obj["pwm"].get("warm", 0)
                status["white"] = obj["pwm"].get("white", 0)
            if "ambient" in obj:
                status["ambient"] = obj["ambient"]
            if "touch_hint" in obj:
                status["touch"] = obj["touch_hint"]
            self.comm.update_status.emit(status)
        except json.JSONDecodeError:
            # 非 JSON 的原始文本，直接记录显示
            self.comm.update_log.emit(f"收到数据: {data}", "black")

    def send_cmd(self, warm, white):
        if not self.client_socket:
            return False
        try:
            cmd = json.dumps({"pwm": {"warm": warm, "white": white}}) + "\n"
            self.client_socket.send(cmd.encode('utf-8'))
            return True
        except (ConnectionResetError, BrokenPipeError):
            self.client_socket.close()
            self.client_socket = None
            self.comm.update_log.emit("发送失败，连接断开", "red")
            return False

    # 修复：添加缺失的stop方法
    def stop(self):
        self.running = False
        if self.client_socket:
            self.client_socket.close()
        self.server_socket.close()
        self.quit()  # QThread标准停止方法
        self.wait()


# -------------------------- 主窗口 --------------------------
class LightControlWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("灯光控制")
        self.setFixedSize(650, 300)
        self.setStyleSheet("""
            QWidget {font-family: SimHei; font-size: 12px;}
            QPushButton {padding: 6px 12px; background: #4080FF; color: white; border: none; border-radius: 4px;}
            QPushButton:hover {background: #3070EE;}
            QFrame {border: 1px solid #E0E0E0; border-radius: 4px; padding: 10px;}
            QLabel#log_label {min-height: 20px;}
        """)

        # 初始化实例属性
        self.network_thread = None
        self.host = ""
        self.port = 0
        self.host_label = None
        self.port_label = None
        self.log_label = None
        self.warm_display = None
        self.white_display = None
        self.ambient_display = None
        self.touch_display = None
        self.warm_input = None
        self.white_input = None

        self.load_or_init_config()
        self.build_flat_ui()
        self.start_network_thread()

    def load_or_init_config(self):
        if not os.path.exists(CONFIG_PATH):
            # 默认在所有网卡监听 9000 端口，匹配 ESP8266 的配置
            config["NETWORK"] = {"host": "0.0.0.0", "port": "9000"}
            with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                config.write(f)
        config.read(CONFIG_PATH)
        self.host = config["NETWORK"]["host"]
        self.port = int(config["NETWORK"]["port"])

    def build_flat_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setSpacing(15)
        main_layout.setContentsMargins(20, 20, 20, 20)

        # 顶部布局
        top_layout = QHBoxLayout()
        self.host_label = QLabel(f"服务端IP: {self.host}")
        self.port_label = QLabel(f"端口: {self.port}")
        config_btn = QPushButton("修改配置")
        config_btn.clicked.connect(self.edit_config)
        self.log_label = QLabel("等待ESP8266连接...")
        self.log_label.setObjectName("log_label")
        self.log_label.setStyleSheet("color: blue;")
        top_layout.addWidget(self.host_label)
        top_layout.addWidget(self.port_label)
        top_layout.addWidget(config_btn)
        top_layout.addStretch()
        top_layout.addWidget(self.log_label)
        main_layout.addLayout(top_layout)

        # 状态布局
        status_frame = QFrame()
        status_layout = QHBoxLayout(status_frame)
        status_layout.setSpacing(20)

        # 暖光
        warm_layout = QVBoxLayout()
        warm_layout.addWidget(QLabel("暖光PWM值"))
        self.warm_display = QLabel("0")
        self.warm_display.setAlignment(Qt.AlignCenter)
        self.warm_display.setStyleSheet("font-size: 18px; color: #FF8C00;")
        warm_layout.addWidget(self.warm_display)
        status_layout.addLayout(warm_layout)

        # 白光
        white_layout = QVBoxLayout()
        white_layout.addWidget(QLabel("白光PWM值"))
        self.white_display = QLabel("0")
        self.white_display.setAlignment(Qt.AlignCenter)
        self.white_display.setStyleSheet("font-size: 18px; color: #4080FF;")
        white_layout.addWidget(self.white_display)
        status_layout.addLayout(white_layout)

        # 环境光
        ambient_layout = QVBoxLayout()
        ambient_layout.addWidget(QLabel("环境光(lux)"))
        self.ambient_display = QLabel("0")
        self.ambient_display.setAlignment(Qt.AlignCenter)
        self.ambient_display.setStyleSheet("font-size: 18px; color: #20C020;")
        ambient_layout.addWidget(self.ambient_display)
        status_layout.addLayout(ambient_layout)

        # 触控
        touch_layout = QVBoxLayout()
        touch_layout.addWidget(QLabel("触控状态"))
        self.touch_display = QLabel("无操作")
        self.touch_display.setAlignment(Qt.AlignCenter)
        self.touch_display.setStyleSheet("font-size: 14px; color: #666;")
        touch_layout.addWidget(self.touch_display)
        status_layout.addLayout(touch_layout)
        main_layout.addWidget(status_frame)

        # 控制布局
        control_layout = QHBoxLayout()
        control_layout.addWidget(QLabel("暖光PWM(0-100):"))
        self.warm_input = QLineEdit("0")
        self.warm_input.setFixedWidth(60)
        control_layout.addWidget(self.warm_input)
        control_layout.addWidget(QLabel("白光PWM(0-100):"))
        self.white_input = QLineEdit("0")
        self.white_input.setFixedWidth(60)
        control_layout.addWidget(self.white_input)
        send_btn = QPushButton("发送控制指令")
        send_btn.clicked.connect(self.send_control_cmd)
        control_layout.addWidget(send_btn)
        clear_btn = QPushButton("清空输入")
        clear_btn.clicked.connect(lambda: (self.warm_input.setText("0"), self.white_input.setText("0")))
        control_layout.addWidget(clear_btn)
        control_layout.addStretch()
        main_layout.addLayout(control_layout)

    def start_network_thread(self):
        self.network_thread = NetworkThread(self.host, self.port)
        self.network_thread.comm.update_status.connect(self.update_device_status)
        self.network_thread.comm.update_log.connect(self.update_log_display)
        self.network_thread.start()

    def update_device_status(self, status):
        if "warm" in status:
            self.warm_display.setText(str(status["warm"]))
        if "white" in status:
            self.white_display.setText(str(status["white"]))
        if "ambient" in status:
            self.ambient_display.setText(str(status["ambient"]))
        if "touch" in status:
            self.touch_display.setText(status["touch"])

    def update_log_display(self, text, color):
        self.log_label.setText(text)
        self.log_label.setStyleSheet(f"color: {color};")

    def send_control_cmd(self):
        try:
            warm = int(self.warm_input.text())
            white = int(self.white_input.text())
            if not (0 <= warm <= 100 and 0 <= white <= 100):
                QMessageBox.warning(self, "输入错误", "PWM值必须在0-100之间")
                return
            if self.network_thread.send_cmd(warm, white):
                self.update_log_display(f"已发送: 暖光{warm} | 白光{white}", "green")
            else:
                QMessageBox.warning(self, "发送失败", "未连接到ESP8266")
        except ValueError:
            QMessageBox.warning(self, "输入错误", "请输入有效的数字")

    def edit_config(self):
        dlg = QDialog(self)
        dlg.setWindowTitle("修改网络配置")
        dlg.setFixedSize(300, 150)
        host_edit = QLineEdit(self.host)
        port_edit = QLineEdit(str(self.port))
        layout = QFormLayout()
        layout.addRow("服务端IP:", host_edit)
        layout.addRow("服务端端口:", port_edit)
        # 修复拼写错误：btns→buttons
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(dlg.accept)
        buttons.rejected.connect(dlg.reject)
        main_layout = QVBoxLayout(dlg)
        main_layout.addLayout(layout)
        main_layout.addWidget(buttons)

        if dlg.exec() == QDialog.Accepted:
            try:
                new_port = int(port_edit.text())
                if not (1 <= new_port <= 65535):
                    QMessageBox.warning(self, "错误", "端口必须是1-65535的数字")
                    return
            except ValueError:
                QMessageBox.warning(self, "错误", "端口必须是数字")
                return
            config["NETWORK"] = {"host": host_edit.text().strip(), "port": str(new_port)}
            with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                config.write(f)
            self.network_thread.stop()
            self.host = host_edit.text().strip()
            self.port = new_port
            self.host_label.setText(f"服务端IP: {self.host}")
            self.port_label.setText(f"端口: {self.port}")
            self.start_network_thread()
            self.update_log_display("配置已更新，重启服务端", "green")

    def closeEvent(self, event):
        # 修复：先判断network_thread是否存在
        if self.network_thread and self.network_thread.isRunning():
            self.network_thread.stop()
        event.accept()


# -------------------------- 程序入口 --------------------------
if __name__ == "__main__":
    # 强制指定PySide6插件路径（解决EXE运行问题）
    if getattr(sys, 'frozen', False):
        os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = os.path.join(
            os.path.dirname(sys.executable), "PySide6", "plugins"
        )

    app = QApplication(sys.argv)
    app.setFont(QFont("SimHei", 12))
    window = LightControlWindow()
    window.show()
    sys.exit(app.exec())