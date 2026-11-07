# -*- coding: utf-8 -*-
"""四轴机械臂坐标抓取窗口（USB-TTL: COM11, 9600 8N1）"""
import queue
import threading
import tkinter as tk
from tkinter import ttk, messagebox

import serial
from serial.tools import list_ports

BAUD = 9600
DEFAULT_PORT = "COM11"


class GrabApp:
    def __init__(self, root):
        self.root = root
        self.root.title("机械臂坐标抓取")
        self.root.geometry("470x410")
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

        self.ser = None
        self.running = True
        self.q = queue.Queue()
        self.moving = False

        self.port_var = tk.StringVar(value=DEFAULT_PORT)
        self.x_var = tk.StringVar(value="200")
        self.y_var = tk.StringVar(value="0")
        self.z_var = tk.StringVar(value="80")
        self.status_var = tk.StringVar(value="未连接")

        self.build_ui()
        self.refresh_ports()
        self.root.after(150, self.auto_connect)
        self.root.after(50, self.poll)

    def build_ui(self):
        top = ttk.Frame(self.root)
        top.pack(fill="x", padx=8, pady=6)
        ttk.Label(top, text="串口").pack(side="left")
        self.cb_port = ttk.Combobox(top, textvariable=self.port_var, width=26, state="readonly")
        self.cb_port.pack(side="left", padx=4)
        ttk.Button(top, text="刷新", command=self.refresh_ports).pack(side="left", padx=2)
        self.btn_conn = ttk.Button(top, text="连接", command=self.connect)
        self.btn_conn.pack(side="left", padx=2)

        mid = ttk.Frame(self.root)
        mid.pack(fill="x", padx=8, pady=8)
        ttk.Label(mid, text="X (mm)").grid(row=0, column=0, padx=3)
        ttk.Entry(mid, textvariable=self.x_var, width=9).grid(row=0, column=1, padx=3)
        ttk.Label(mid, text="Y (mm)").grid(row=0, column=2, padx=3)
        ttk.Entry(mid, textvariable=self.y_var, width=9).grid(row=0, column=3, padx=3)
        ttk.Label(mid, text="Z (mm)").grid(row=0, column=4, padx=3)
        ttk.Entry(mid, textvariable=self.z_var, width=9).grid(row=0, column=5, padx=3)

        self.btn_grab = ttk.Button(self.root, text="抓取", command=self.grab)
        self.btn_grab.pack(fill="x", padx=12, pady=6)

        ttk.Label(self.root, textvariable=self.status_var, foreground="#0055aa").pack(anchor="w", padx=12)
        ttk.Label(
            self.root,
            text="原点=底座中心；X 向前，Y 向左，Z 向上；输入的是腕部目标点。",
            foreground="#666666",
        ).pack(anchor="w", padx=12, pady=(2, 0))
        ttk.Label(
            self.root,
            text="初次使用建议空载：X 180~220，Y -80~80，Z 60~150。",
            foreground="#666666",
        ).pack(anchor="w", padx=12)

        self.log = tk.Text(self.root, height=9, state="disabled")
        self.log.pack(fill="both", expand=True, padx=12, pady=8)

    def log_line(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def refresh_ports(self):
        try:
            ports = [p.device for p in list_ports.comports()]
        except Exception as e:
            ports = []
            self.log_line(f"枚举串口失败：{e}")
        self.cb_port["values"] = ports
        if ports and self.port_var.get() not in ports:
            self.port_var.set(DEFAULT_PORT if DEFAULT_PORT in ports else ports[0])
        if not ports:
            self.status_var.set("没有找到串口")

    def auto_connect(self):
        values = list(self.cb_port["values"])
        if DEFAULT_PORT in values:
            self.port_var.set(DEFAULT_PORT)
            self.connect()

    def connect(self):
        port = self.port_var.get().strip()
        if not port:
            messagebox.showwarning("提示", "先选择一个串口")
            return
        try:
            if self.ser is not None and self.ser.is_open:
                self.ser.close()
            self.ser = serial.Serial(port, BAUD, timeout=0.2)
        except Exception as e:
            self.ser = None
            self.status_var.set("连接失败")
            self.log_line(f"连接 {port} 失败：{e}")
            messagebox.showerror(
                "连接失败",
                f"{port}: {e}\n\n如果提示拒绝访问，先关掉占用该串口的终端或串口助手。",
            )
            return
        self.status_var.set(f"已连接 {port} @ {BAUD}")
        self.log_line(f"已连接 {port}")
        self.btn_conn.configure(text="重新连接")
        threading.Thread(target=self.reader_loop, daemon=True).start()

    def reader_loop(self):
        ser = self.ser
        while self.running and ser is not None and ser.is_open:
            try:
                raw = ser.readline()
            except Exception as e:
                self.q.put(("error", str(e)))
                return
            if not raw:
                continue
            text = raw.decode("ascii", errors="replace").strip()
            if text:
                self.q.put(("line", text))

    def poll(self):
        try:
            while True:
                kind, text = self.q.get_nowait()
                if kind == "line":
                    self.log_line(text)
                    if text.startswith("OK"):
                        self.status_var.set("动作中…")
                    elif text.startswith("DONE"):
                        self.status_var.set("完成")
                        self.set_moving(False)
                    elif text.startswith("ERR"):
                        self.status_var.set(text)
                        self.set_moving(False)
                else:
                    self.log_line(f"串口错误：{text}")
                    self.status_var.set("串口断开")
                    self.set_moving(False)
        except queue.Empty:
            pass
        self.root.after(50, self.poll)

    def grab(self):
        if self.moving:
            return
        if self.ser is None or not self.ser.is_open:
            messagebox.showwarning("未连接", "先连接串口")
            return
        try:
            x = float(self.x_var.get())
            y = float(self.y_var.get())
            z = float(self.z_var.get())
        except ValueError:
            messagebox.showerror("格式错误", "X / Y / Z 必须是数字，单位 mm")
            return
        cmd = f"{x:g},{y:g},{z:g}\n"
        try:
            self.ser.write(cmd.encode("ascii"))
        except Exception as e:
            messagebox.showerror("发送失败", str(e))
            return
        self.log_line("> " + cmd.strip())
        self.status_var.set("已发送，等待动作…")
        self.set_moving(True)
        self.root.after(15000, self.timeout_reset)

    def set_moving(self, moving):
        self.moving = moving
        self.btn_grab.configure(state="disabled" if moving else "normal")

    def timeout_reset(self):
        if self.moving:
            self.set_moving(False)
            self.status_var.set("超时未收到 DONE（检查舵机供电/接线）")

    def on_close(self):
        self.running = False
        try:
            if self.ser is not None and self.ser.is_open:
                self.ser.close()
        except Exception:
            pass
        self.root.destroy()


def main():
    root = tk.Tk()
    GrabApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
