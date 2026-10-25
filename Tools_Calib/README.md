# Charger Calib & Protection Tool 1.7

Tool Windows cho firmware `Charger_Protection_v2`, giao thức `com` v1.

Bản 1.7 thêm `leakage_over_count_max` trong tab **Debug**, đơn vị mẫu,
đọc từ firmware 2.7. Đây là chuỗi mẫu sau lọc vượt ngưỡng fast dài nhất
đã ghi nhận, giữ đến Reset MCU (0..20 với N hiện tại). Ví dụ MAX=3 nghĩa
đã có ba mẫu liên tiếp vượt ngưỡng; MAX=20 nghĩa đã đủ điều kiện chốt fast.
Trước khi offset sẵn sàng hiện "Đang học offset"; firmware cũ hiện "Chưa hỗ trợ".

Bản 1.6 chuyển hai giá trị Ileak MAX, bốn ADC offset và hai giá trị ADC
RMS/nhiệt độ sang tab **Debug**. Bảng số đo MCU giữ các đại lượng vận hành.
Thông số Debug vẫn cập nhật khi tab đang ẩn và giữ đơn vị mA/V/count.

Bản 1.5 đặt ba nút **Lưu hệ số mới vào Flash**, **Lưu mặc định** và
**Reset MCU / áp dụng** bên trong tab **Calib hệ số đo**, dưới phần tính
hệ số chuẩn. Chuyển sang tab Protection sẽ ẩn nhóm nút này.

Bản 1.4 thêm **Ileak đỉnh MAX** và **Ileak RMS MAX**, đơn vị mA, đọc từ
MCU mỗi lượt polling. Đỉnh MAX là độ lệch tuyệt đối sau LPF so với offset
fast, bắt đầu khi offset sẵn sàng; RMS MAX gồm cả cửa sổ khởi động. Hai
giá trị giữ tới Reset MCU, không mất khi ngắt/kết nối lại tool và không ghi
Flash. Dòng đỉnh hiện "Đang học offset" trước khi sẵn sàng. Firmware cũ
hiện "Chưa hỗ trợ"; các chức năng cũ vẫn dùng bình thường. Tool cũ vẫn
đọc được firmware mới vì payload MEAS cũ không đổi.

Bản 1.3 hiển thị tên từng lỗi bảo vệ bằng tiếng Việt, kèm hướng xử lý và
mã hex để kiểm tra. Ví dụ `0x00000048`: dòng rò vượt ngưỡng + điện áp vào
thấp. Fault là lỗi đã ghi nhận/giữ chốt, không nhất thiết là số đo hiện tại
còn vượt ngưỡng. Quá dòng, dòng rò và lỗi phép đo cần reset sau khi xử lý
nguyên nhân; lỗi điện áp/nhiệt độ có thể tự phục hồi theo cấu hình. Các cờ
ADC, dòng rò nhanh và cảm biến nhiệt cũng được hiển thị bằng chữ.

Bản 1.2 giữ nút và ô nhập ổn định khi tự đọc số đo mỗi 0,5 s, chỉ cập nhật
ô số đo có thay đổi. Lệnh thủ công được xếp sau lượt đọc nền đang chạy,
không giao tiếp UART đồng thời. Chỉ lệnh thủ công mới khóa các nút thao tác.

## Cấu hình ngưỡng Protection

Firmware 2.1 bổ sung tab **Ngưỡng Protection**, gồm ngưỡng quá áp, thấp áp,
quá dòng, dòng rò RMS, quá nhiệt, ba ngưỡng phục hồi, dòng rò nhanh (đỉnh)
và thời gian giữ ổn định để phục hồi. Mặc định vẫn như firmware trước:
240/190 V, 2,8 A, 30 mA RMS, 70 °C; phục hồi 230/200 V, 65 °C trong
1000 ms; dòng rò nhanh 30 mA đỉnh. Cả hai ô dòng rò dùng mA trên giao diện.

1. Kết nối MCU, mở tab **Ngưỡng Protection**.
2. Sửa **Giá trị mới**, bấm **Lưu ngưỡng vào Flash**. Tool xác nhận giá trị,
   MCU ghi Flash, tool đọc lại đối chiếu từng byte. Cột **Đang chạy** vẫn là
   bộ cũ; cột **Trong Flash** là bộ chờ áp dụng.
3. Bấm **Reset MCU / áp dụng**. Lần khởi động sau dùng bộ ngưỡng mới.

Chưa có bản ghi hợp lệ thì tự dùng mặc định; không cần lưu để chạy lần đầu.
Record Calib v1 cũ vẫn đọc được, ngưỡng dùng mặc định. Lưu/khôi phục ngưỡng
giữ nguyên Calib đã lưu, và ngược lại, kể cả khi nhóm kia đang chờ reset.
Nút **Khôi phục ngưỡng mặc định** chỉ khôi phục Protection. JSON hiện dành
cho hệ số Calib. Firmware cũ thiếu capability sẽ vô hiệu hóa ô/nút Protection.

Giới hạn: `1 <= UV < UV phục hồi < OV phục hồi < OV <= 400 V`;
quá dòng 0,01..30 A; mỗi ngưỡng rò 1..1000 mA;
`-40 <= nhiệt phục hồi < nhiệt cắt <= 150 °C`; thời gian nguyên 1..65535 ms.
Các giới hạn này là kiểm tra phần mềm; chọn ngưỡng phù hợp board/tải thực tế.
Lưu thay đổi mở relay và dừng đo tới khi reset. Ngưỡng RMS và ngưỡng đỉnh
độc lập; tăng riêng ngưỡng RMS không đổi đường ngắt rò nhanh.

Vẫn dùng duy nhất trang Flash `0x08003C00..0x08003FFF`. Record mới v2 có
80 byte, CRC32 và commit cuối; firmware cũ không đọc được v2. Mất nguồn sau
khi xóa có thể mất cả hai nhóm; boot về mặc định nếu record lỗi. Khi nâng cấp
muốn giữ dữ liệu, chỉ erase các trang firmware, tránh mass erase.

## Chạy

- Mở bản cập nhật `dist/ChargerCalibTool_1.7.exe`; đóng cửa sổ tool cũ trước khi kết nối COM. Khi tự build, tên đầu ra là `dist/ChargerCalibTool.exe`. Các bản exe đã đóng gói Python, Tkinter và pyserial; máy sử dụng không cần cài Python.
- Kết nối USB–UART mức logic **3.3 V**: MCU **PA9 (TX) → RX adapter**, **PA10 (RX) ← TX adapter**, GND chung. UART **115200, 8N1**, không flow control. Không dùng trực tiếp tín hiệu điện áp RS-232.
- Chọn cổng COM → **Kết nối**. Tool đọc trạng thái, hệ số đang chạy, hệ số trong Flash và số đo. Tự đọc số đo mỗi 0.5 s; giao dịch UART chạy ở luồng nền.

## Calib một mạch

1. Chờ MCU có cửa sổ đo hợp lệ và nguồn/tải/giá trị chuẩn ổn định. Đọc giá trị bằng thiết bị chuẩn tại cùng thời điểm.
2. Nhập giá trị chuẩn cho từng kênh, bấm **Tính hệ số**. Có thể sửa trực tiếp cột **Hệ số mới**. Các kênh khác giữ nguyên hệ số đã đọc khi kết nối.
3. **Lưu hệ số mới vào Flash**. Tool kiểm tra giới hạn, gửi 5 hệ số float32 và đọc lại Flash để xác nhận từng byte. Khi có thay đổi, MCU mở relay và dừng ADC/DMA đến khi reset. Số đo hiển thị lúc này là số đo cuối, được đánh dấu đã dừng.
4. **Reset MCU / áp dụng**. MCU trả ACK đầy đủ rồi mới reset, đọc hệ số từ Flash ở boot; tool đọc lại trạng thái và hệ số đang chạy.

**Gain mới = gain đang chạy × giá trị chuẩn / số đo đang chạy.**
**Offset nhiệt độ mới = offset đang chạy + nhiệt độ chuẩn − số đo nhiệt độ.**

Gain là bù thêm lên các hệ số chuyển đổi phần cứng ban đầu. Mặc định 4 gain = 1, offset = 0, giữ nguyên cách tính trước khi thêm Calib. Giới hạn gain 0.25…4, offset −20…20 °C. Tool chặn tính khi chưa có số đo mới, chưa khởi động xong, đang chờ reset, kênh clipping hoặc lỗi NTC. Có thể lưu JSON để giữ bộ hệ số theo từng mạch; nhập JSON chỉ thay đổi các ô chờ ghi.

Đơn vị dây truyền: điện áp V, dòng **A** (kể cả dòng rò), công suất W/VA, tần số Hz, nhiệt độ °C; 4 ADC DC offset là điện áp V tại chân ADC. Giao diện đổi dòng rò sang **mA**, cả số đo và ô nhập giá trị chuẩn. ADC RMS dòng rò và ADC nhiệt độ dùng count.

**Lưu mặc định** ghi gain=1, offset=0 vào page Calib và cũng cần reset nếu dữ liệu thay đổi. `UNCHANGED` không erase/program lại và không tự dừng đo. Lưu một bộ mới dùng một page Flash 1 KB; mất nguồn giữa lúc erase/write có thể làm mất bản cũ. Boot sẽ dùng mặc định nếu record không hợp lệ.

Nếu timeout sau khi ghi, trạng thái ghi chưa xác định: bấm **Đọc lại** để kiểm tra cột Trong Flash và trạng thái cần reset. Tool không tự gửi lại SAVE/DEFAULTS/RESET. Không dùng nhiều chương trình cùng mở một COM. Lưu Flash chỉ thực hiện khi người dùng bấm nút, không lưu theo chu kỳ.

## Chạy từ mã Python / build lại

Yêu cầu Python có Tkinter (bản build hiện tại dùng Python 3.13.12).

```powershell
python -m pip install -r requirements.txt
python charger_tool.py

python -m pip install -r requirements-build.txt
./build_tool.ps1 -Python python
```

Chạy các lệnh từ thư mục tool. Runtime cục bộ: `.python/python.exe`.
Runtime này không cần chép sang máy sử dụng. `build_tool.ps1` cũng kiểm tra
khởi tạo bản exe sau khi đóng gói.

```powershell
./.python/python.exe -m unittest discover -s tests -p test_protocol.py -v
./.python/python.exe charger_tool.py --smoke-test
./.python/python.exe -m unittest discover -s tests -p gui_smoke.py -v
```

Bộ test giao thức biên dịch **com.c, Calib.c, Rte.c thật** với USART/Flash/reset giả lập bằng GCC, rồi Python gửi và giải mã các frame giữa hai phía. Có kiểm tra CRC, noise, timeout, tràn buffer, lỗi UART, giới hạn hệ số, Flash lỗi, active/stored và reset đợi TC. Mặc định host GCC Windows là `C:/mingw64/bin/gcc.exe`; đổi qua biến môi trường `HOST_GCC` nếu cần.

Tài liệu byte giao thức: `software/Charger_Protection_v2/Core/Com/README.md`.
Kiểm thử gồm 15 bài giao thức và 6 bài giao diện với mã MCU thực trên host.
Chưa thay thế kiểm tra UART/Flash trên bo mạch thật.
