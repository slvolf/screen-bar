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
import time
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QFrame, QMessageBox, QDialog,
    QDialogButtonBox, QFormLayout, QSlider, QLineEdit
)
from PySide6.QtCore import Signal, QObject, Qt, QThread
from PySide6.QtGui import QFont

MIN_PWM = 3
MAX_PWM = 100
DEFAULT_BRIGHTNESS = 50  # 开关开启时默认亮度（首次开启）

def get_config_path():
    if getattr(sys, 'frozen', False):
        return os.path.join(os.path.dirname(sys.executable), "config.ini")
    return "config.ini"

CONFIG_PATH = get_config_path()
config = configparser.ConfigParser()

class Communicate(QObject):
    update_status = Signal(dict)
    update_log = Signal(str, str)

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
        self.suppress_until = 0.0
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
                text = chunk.decode('utf-8', errors='ignore')
                if text:
                    self.recv_buffer += text
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
        if time.time() < self.suppress_until:
            return  # 暂停处理上报，避免立即覆盖
        try:
            obj = json.loads(data)
            status = {}
            if "pwm" in obj:
                status["warm"] = obj["pwm"].get("warm", 0)
                status["white"] = obj["pwm"].get("white", 0)
            if "ambient" in obj:
                status["ambient"] = obj["ambient"]
            if "touch" in obj:
                status["touch"] = obj["touch"]
            self.comm.update_status.emit(status)
        except json.JSONDecodeError:
            self.comm.update_log.emit(f"收到数据: {data}", "black")

    def send_cmd(self, warm, white):
        if not self.client_socket:
            return False
        try:
            cmd = json.dumps({"cmd": "set_pwm", "warm": warm, "white": white}, separators=(",", ":")) + "\n"
            self.client_socket.send(cmd.encode('utf-8'))
            self.suppress_until = time.time() + 0.3
            return True
        except (ConnectionResetError, BrokenPipeError):
            self.client_socket.close()
            self.client_socket = None
            self.comm.update_log.emit("发送失败，连接断开", "red")
            return False

    def stop(self):
        self.running = False
        if self.client_socket:
            self.client_socket.close()
        self.server_socket.close()
        self.quit()
        self.wait()

class LightControlWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("灯光控制")
        self.setMinimumSize(720, 370)
        self.resize(760, 370)
        self.setStyleSheet("""
            QWidget {font-family: 'Microsoft YaHei'; font-size: 12px; background: #111;}
            QLabel {color: #eee;}
            QPushButton {padding: 6px 12px; background: #4080FF; color: white; border: none; border-radius: 4px;}
            QPushButton:hover {background: #3070EE;}
            QPushButton#off_btn {background: #666; color: #eee;}
            QPushButton#off_btn:hover {background: #555;}
            QFrame {border: 1px solid #333; border-radius: 6px; padding: 10px; background: #1b1b1b;}
            QLabel#log_label {min-height: 20px; color: #4da3ff;}
            QSlider::groove:horizontal {height: 6px; background: #333; border-radius: 3px;}
            QSlider::handle:horizontal {background: #4da3ff; width: 16px; height: 16px; margin: -6px 0; border-radius: 8px;}
            QSlider::sub-page:horizontal {background: #4da3ff;}
        """)

        self.network_thread = None
        self.host = ""
        self.port = 0
        self.log_label = None
        self.ambient_display = None
        self.touch_display = None
        self.warm_slider = None
        self.white_slider = None
        self.warm_value_label = None
        self.white_value_label = None
        # 新增：开关状态与上次亮度记录
        self.warm_switch_btn = None
        self.white_switch_btn = None
        self.last_warm_brightness = DEFAULT_BRIGHTNESS  # 暖光上次开启亮度
        self.last_white_brightness = DEFAULT_BRIGHTNESS  # 白光上次开启亮度

        self.load_or_init_config()
        self.build_ui()
        self.start_network_thread()

    def load_or_init_config(self):
        if not os.path.exists(CONFIG_PATH):
            config["NETWORK"] = {"host": "0.0.0.0", "port": "9000"}
            with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                config.write(f)
        config.read(CONFIG_PATH)
        self.host = config["NETWORK"]["host"]
        self.port = int(config["NETWORK"]["port"])

    def build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main = QVBoxLayout(central)
        main.setSpacing(12)
        main.setContentsMargins(16, 16, 16, 16)

        top = QHBoxLayout()
        top.addWidget(QLabel(f"服务端IP: {self.host}"))
        top.addWidget(QLabel(f"端口: {self.port}"))
        self.log_label = QLabel("等待ESP8266连接...")
        self.log_label.setObjectName("log_label")
        top.addStretch()
        top.addWidget(self.log_label)
        main.addLayout(top)

        # 状态帧（环境光、触控状态、开关按钮在同一行）
        status_frame = QFrame()
        status_layout = QHBoxLayout(status_frame)
        status_layout.setSpacing(18)

        def build_block(title, color="#eee"):
            box = QVBoxLayout()
            t = QLabel(title)
            val = QLabel("0")
            val.setAlignment(Qt.AlignCenter)
            val.setStyleSheet(f"font-size: 20px; color: {color};")
            box.addWidget(t)
            box.addWidget(val)
            return box, val

        # 环境光模块
        amb_box, self.ambient_display = build_block("环境光(lux)", "#65d46e")
        # 触控状态模块
        touch_box, self.touch_display = build_block("触控状态", "#aaa")
        # 新增：开关按钮模块（水平布局容纳两个开关）
        switch_box = QVBoxLayout()
        switch_title = QLabel("灯光开关")
        switch_btn_layout = QHBoxLayout()
        switch_btn_layout.setSpacing(12)

        # 暖光开关
        self.warm_switch_btn = QPushButton("开启暖光")
        self.warm_switch_btn.setObjectName("off_btn")  # 初始为关闭样式
        self.warm_switch_btn.clicked.connect(self.on_warm_switch_clicked)
        # 白光开关
        self.white_switch_btn = QPushButton("开启白光")
        self.white_switch_btn.setObjectName("off_btn")  # 初始为关闭样式
        self.white_switch_btn.clicked.connect(self.on_white_switch_clicked)

        switch_btn_layout.addWidget(self.warm_switch_btn)
        switch_btn_layout.addWidget(self.white_switch_btn)
        switch_box.addWidget(switch_title)
        switch_box.addLayout(switch_btn_layout)

        # 将三个模块加入同一行
        status_layout.addLayout(amb_box)
        status_layout.addLayout(touch_box)
        status_layout.addLayout(switch_box)  # 开关按钮加入环境光一行
        status_layout.addStretch()
        main.addWidget(status_frame)

        # 控制帧（仅保留滑块）
        ctrl_frame = QFrame()
        ctrl_layout = QVBoxLayout(ctrl_frame)
        ctrl_layout.setSpacing(14)

        # 原有：滑块调节行
        def build_slider_row(label_text, is_warm):
            row = QHBoxLayout()
            label = QLabel(label_text)
            slider = QSlider(Qt.Horizontal)
            slider.setRange(MIN_PWM, MAX_PWM)
            slider.setTickInterval(1)
            slider.setSingleStep(1)
            slider.setValue(MIN_PWM)
            value_label = QLabel(str(MIN_PWM))
            value_label.setFixedWidth(50)
            value_label.setAlignment(Qt.AlignCenter)
            slider.valueChanged.connect(lambda v: value_label.setText(str(v)))
            slider.valueChanged.connect(self.send_control_cmd)  # 实时发送
            if is_warm:
                self.warm_slider = slider
                self.warm_value_label = value_label
            else:
                self.white_slider = slider
                self.white_value_label = value_label
            row.addWidget(label)
            row.addWidget(slider, 1)
            row.addWidget(value_label)
            return row

        ctrl_layout.addLayout(build_slider_row("暖光PWM (3-100)", True))
        ctrl_layout.addLayout(build_slider_row("白光PWM (3-100)", False))
        main.addWidget(ctrl_frame)

    def set_slider_value(self, slider, value):
        slider.blockSignals(True)
        slider.setValue(value)
        slider.blockSignals(False)

    def start_network_thread(self):
        self.network_thread = NetworkThread(self.host, self.port)
        self.network_thread.comm.update_status.connect(self.update_device_status)
        self.network_thread.comm.update_log.connect(self.update_log_display)
        self.network_thread.start()

    def update_device_status(self, status):
        if "warm" in status:
            v = max(MIN_PWM, min(MAX_PWM, int(status["warm"])))
            self.set_slider_value(self.warm_slider, v)
            if self.warm_value_label:
                self.warm_value_label.setText(str(v))
            # 同步开关按钮状态
            self.update_switch_btn_state(self.warm_switch_btn, v != MIN_PWM)
            if v != MIN_PWM:
                self.last_warm_brightness = v  # 更新上次亮度
        if "white" in status:
            v = max(MIN_PWM, min(MAX_PWM, int(status["white"])))
            self.set_slider_value(self.white_slider, v)
            if self.white_value_label:
                self.white_value_label.setText(str(v))
            # 同步开关按钮状态
            self.update_switch_btn_state(self.white_switch_btn, v != MIN_PWM)
            if v != MIN_PWM:
                self.last_white_brightness = v  # 更新上次亮度
        if "ambient" in status:
            self.ambient_display.setText(str(status["ambient"]))
        if "touch" in status:
            self.touch_display.setText(str(status["touch"]))

    def update_log_display(self, text, color):
        self.log_label.setText(text)
        self.log_label.setStyleSheet(f"color: {color};")

    def send_control_cmd(self):
        warm = self.warm_slider.value()
        white = self.white_slider.value()
        # 同步开关按钮状态
        self.update_switch_btn_state(self.warm_switch_btn, warm != MIN_PWM)
        self.update_switch_btn_state(self.white_switch_btn, white != MIN_PWM)
        # 更新上次亮度
        if warm != MIN_PWM:
            self.last_warm_brightness = warm
        if white != MIN_PWM:
            self.last_white_brightness = white
        # 发送指令
        if self.network_thread and self.network_thread.send_cmd(warm, white):
            self.update_log_display(f"已发送: 暖光{warm} | 白光{white}", "green")
        else:
            self.update_log_display("未连接到ESP8266", "red")

    # 暖光开关点击事件
    def on_warm_switch_clicked(self):
        current_warm = self.warm_slider.value()
        if current_warm == MIN_PWM:
            # 当前关闭，点击开启（恢复上次亮度）
            self.set_slider_value(self.warm_slider, self.last_warm_brightness)
            self.warm_value_label.setText(str(self.last_warm_brightness))
            self.update_log_display(f"暖光已开启（亮度：{self.last_warm_brightness}）", "green")
        else:
            # 当前开启，点击关闭（记录当前亮度，设为MIN_PWM）
            self.last_warm_brightness = current_warm
            self.set_slider_value(self.warm_slider, MIN_PWM)
            self.warm_value_label.setText(str(MIN_PWM))
            self.update_log_display("暖光已关闭", "red")
        # 发送指令
        self.send_control_cmd()

    # 白光开关点击事件
    def on_white_switch_clicked(self):
        current_white = self.white_slider.value()
        if current_white == MIN_PWM:
            # 当前关闭，点击开启（恢复上次亮度）
            self.set_slider_value(self.white_slider, self.last_white_brightness)
            self.white_value_label.setText(str(self.last_white_brightness))
            self.update_log_display(f"白光已开启（亮度：{self.last_white_brightness}）", "green")
        else:
            # 当前开启，点击关闭（记录当前亮度，设为MIN_PWM）
            self.last_white_brightness = current_white
            self.set_slider_value(self.white_slider, MIN_PWM)
            self.white_value_label.setText(str(MIN_PWM))
            self.update_log_display("白光已关闭", "red")
        # 发送指令
        self.send_control_cmd()

    # 更新开关按钮样式与文本
    def update_switch_btn_state(self, btn, is_on):
        if is_on:
            btn.setText(f"关闭{btn.text()[2:]}")  # 从“开启暖光”变为“关闭暖光”
            btn.setObjectName("")  # 移除关闭样式，使用默认开启样式
        else:
            btn.setText(f"开启{btn.text()[2:]}")  # 从“关闭暖光”变为“开启暖光”
            btn.setObjectName("off_btn")  # 设置关闭样式
        # 刷新样式
        btn.style().unpolish(btn)
        btn.style().polish(btn)

    def closeEvent(self, event):
        if self.network_thread and self.network_thread.isRunning():
            self.network_thread.stop()
        event.accept()

if __name__ == "__main__":
    if getattr(sys, 'frozen', False):
        os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = os.path.join(
            os.path.dirname(sys.executable), "PySide6", "plugins"
        )
    app = QApplication(sys.argv)
    app.setFont(QFont("Microsoft YaHei", 12))
    window = LightControlWindow()
    window.show()
    sys.exit(app.exec())