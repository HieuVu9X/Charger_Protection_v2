CHARGER CALIB TOOL 1.0 — WINDOWS x64

Chạy ChargerCalibTool.exe; không cần cài Python.
Firmware tương thích: Charger_Protection_v2, com protocol v1.

Kết nối USB–UART logic 3.3 V:
  PA9 (TX MCU) -> RX adapter
  PA10 (RX MCU) <- TX adapter
  GND chung; 115200 baud, 8 data bits, no parity, 1 stop bit.

1. Chọn cổng COM và Kết nối.
2. Chờ số đo ổn định. Nhập giá trị chuẩn, bấm Tính hệ số từng kênh.
   Vin/Vout: V; Iin: A; dòng rò: mA; nhiệt độ: °C.
3. Lưu hệ số mới vào Flash. Tool đọc lại để kiểm tra.
4. Nếu có thay đổi, MCU mở relay và dừng đo đến khi bấm Reset MCU / áp dụng.

Mặc định gain = 1, offset nhiệt độ = 0: giữ hệ số phần cứng ban đầu.
Calib chỉ bù thêm sai số từng board. Flash Calib dùng một page 1 KB.
Nếu ghi lỗi/mất nguồn sau erase, boot dùng mặc định khi record không hợp lệ.
Nếu timeout sau khi lưu, bấm Đọc lại để kiểm tra Flash; tool không tự gửi lại.

Mã Python: Tools/charger_tool.py và Tools/protocol.py.
Hướng dẫn đầy đủ: Tools/README.md.
Đặc tả giao thức: Core/com/README.md.

Đã build Debug/Release, qua test host và test GUI với MCU mô phỏng.
Chưa kiểm tra UART/Flash trên bo mạch thật.
