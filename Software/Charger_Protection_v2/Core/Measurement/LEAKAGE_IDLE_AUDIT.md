# Kiểm tra dòng rò khi chỉ cấp nguồn 12 V — 2026-10-24

## N=20 — firmware 2.5

Tăng xác nhận từ 10 lên 20 mẫu liên tiếp. Từ mẫu đầu vượt ngưỡng sau LPF
đến mẫu xác nhận là 1,9 ms. Test bước raw 12 count xác nhận ở mẫu thứ 22.
Measurement 581 kiểm tra, Protection/tích hợp và Calib đều qua. Cutoff,
ngưỡng/RMS và giao thức giữ nguyên; các bảng sau là mốc lịch sử.

## N=10 — firmware 2.4

Tăng số mẫu xác nhận liên tiếp từ 5 lên 10. Cutoff, ngưỡng và RMS giữ
nguyên. Test bước raw 12 count xác nhận ở mẫu thứ 12; từ mẫu đầu vượt
ngưỡng sau LPF đến mẫu thứ 10 là 0,9 ms. Measurement 461 kiểm tra và
Protection/tích hợp đều qua. Các bảng audit sau đây ghi ở các mốc cũ.

## Xác nhận N=5 — firmware 2.3

Đã yêu cầu 5 mẫu sau LPF liên tiếp đạt ngưỡng. Các bảng audit sáu ca bên
dưới vẫn cho cùng RMS/cờ/fault sau thay đổi. Test riêng xung raw 50/100
count một mẫu (chỉ kéo dài 3/4 mẫu vượt sau LPF) nay không chốt fast, MAX
vẫn giữ đỉnh. Bước raw 12 count chốt ở mẫu thứ 7 thay vì thứ 3; riêng các
mô tả thời gian ở phần lịch sử bên dưới là của bản trước N=5. Không tắt
RMS khởi động, không tự xóa lỗi, chưa xác nhận nguồn nhiễu thật trên board.

## Cập nhật low-pass ADC — 2026-10-25

Ileak RMS và fast dùng chung LPF ADC 1 kHz; Vin/Vout/Iin có LPF trước bù
RC/căn thời gian, NTC dùng 10 Hz. Raw ADC vẫn dùng chẩn đoán clipping và
ước lượng offset cửa sổ. Audit sau cập nhật:

| Tình huống | RMS cuối [mA] | RMS max [mA] | Fast | Fault cuối |
|---|---:|---:|---:|---|
| DC ổn định 1,6403 V | 0 | 0 | 0 | 0x40 |
| Nhiễu xen kẽ +/-3 count | 0 | 0,330 | 0 | 0x40 |
| Nhiễu 5,376 count RMS, đỉnh 8 count | 4,128 | 4,195 | 0 | 0x40 |
| Một mẫu 8 count sau startup | 0 | 0,367 | 0 | 0x40 |
| Bias dịch 48 count trong 50 ms đầu | 0 | 53,500 | 0 | 0x48 |
| Bias bước 12 count sau 210 ms | 0 | 11,020 | 1 | 0x48 |

Đây là waveform giả lập, không phải bằng chứng nhiễu thật của board đã
được xử lý. LPF làm suy giảm cả xung rò ngắn. Test bước 12 count chốt ở
mẫu thứ ba; chưa gồm DMA/relay, chưa xác nhận trên board. RMS khởi động
vẫn có thể chốt lỗi. Các bảng và thuật toán bên dưới là số liệu lịch sử.

## Cập nhật offset fast — 2026-10-25

Đã thay offset nguyên của cửa sổ trước bằng trạng thái Q19 riêng: học từ
cửa sổ sạch có hai nửa ổn định, sau đó bám chậm theo mẫu (tau ~0,8192 s).
So ngưỡng trước khi học; lỗi fast/RMS giữ offset tới reset. Relay chờ
`fast_leakage_ready`. Xem `README.md` cùng thư mục để biết chi tiết.

Chạy lại sáu ca audit bên dưới vẫn cho cùng RMS/cờ/fault. Đặc biệt, nhiễu
5,376 count RMS với đỉnh 8 count và một mẫu 8 count sau startup vẫn chốt
LEAK. Bước bias 12 count vẫn chốt fast; chuyển tiếp khởi động vẫn có thể
chốt lỗi từ RMS. Sửa offset không loại bỏ nhiễu AC hay tự xóa lỗi đã chốt.
Các mô tả thuật toán/binary trong các phần sau là mốc trước thay đổi này.

## Dữ liệu board người dùng cung cấp

- Không cấp Vin/Vout AC; chỉ cấp 12 V cho mạch.
- Tool báo dòng rò 22..23 mA; ADC RMS dòng rò 5..5,4 count.
- ADC offset Ileak khoảng 1,64 V; cờ dòng rò nhanh đã kích hoạt.
- ADC không chạm biên. Chưa xác định thời điểm cờ xuất hiện sau reset.
- Người dùng xác nhận C31/C32/C33 ở đầu ra LM1117-3.3 thực tế đều 10 nF.

Đây là các giá trị tổng hợp, chưa có waveform ADC/oscilloscope. Không thể
xác định nguồn nhiễu vật lý hoặc thời điểm đỉnh chỉ từ snapshot này.

## Đối chiếu datasheet và mạch

Datasheet ZMCT118F: định mức 5 A / 5 mA, tỷ số 1000:1, dải tuyến tính
0..30 A với tải 50 Ω. Code trước ghi 30 A / 30 mA nhưng cũng cho tỷ số 1000;
sửa hai giá trị định mức theo datasheet không thay đổi thang đo.

Nguồn hãng: https://www.micro-transformer.com/pcb-mounting-current-transformer-zmct118f.html

Theo schematic lưu ở hardware, R28 = 4700 Ω tải song song nhánh đầu vào
R26 + R31 = 200 Ω, tạo tải hiệu dụng khoảng 191,84 Ω ở tần số thấp, với
op-amp lý tưởng và các cặp điện trở khớp. Gain vi sai đang là 1.

    I_primary_per_count = (3,3 / 4096) × 1000 / 191,8367
                        = 0,004199738 A/count

Với leakage_gain mặc định bằng 1:

- 5..5,4 count RMS tương ứng 21,00..22,68 mA RMS: khớp số người dùng gửi.
- Ngưỡng RMS 30 mA tương ứng khoảng 7,14 count RMS.
- Ngưỡng nhanh mặc định 30 mA đỉnh được lượng tử thành 8 count, tương ứng
  33,60 mA đỉnh, hay khoảng 6,45 mV lệch khỏi bias tại ADC.
- Mức 5..5,4 count tương ứng khoảng 4,03..4,35 mV RMS trên ADC. Cần đo
  waveform để xác định biên độ đỉnh, phổ nhiễu và nguồn gây dao động.
- Bias 1,64 V không tự tạo dòng RMS vì trung bình của chính cửa sổ được trừ.
- Không clipping chỉ loại trừ mẫu sát 0/4095; không loại trừ nhiễu vài count.

Các con số phụ thuộc gain/những ngưỡng Đang chạy trên board. Không có bản đọc
Flash trực tiếp trong lần kiểm tra này. Datasheet CT không bảo đảm sai số
ở 30 mA với tải 191,84 Ω; phải xác minh/hiệu chuẩn ở mức dòng quan tâm.

## Thuật toán đang chạy

- PA3 được lấy mẫu 10 kHz, cùng scan PA0..PA4; thứ tự forward và DMA khớp
  thứ tự kênh trong code.
- RMS dùng phương sai trong cửa sổ 200 ms, trừ DC cùng cửa sổ. Vin/Vout và
  dòng tải có bộ lọc số giới hạn băng thông, kênh dòng rò hiện dùng mẫu thô.
- Đường nhanh lấy mẫu PA3 trừ trung bình raw của cửa sổ 200 ms trước. Chỉ
  một mẫu vượt ngưỡng đã đặt leakage_trip_flag. Không xác nhận nhiều mẫu.
- Cờ này được giữ tới reset; Protection chốt lỗi LEAK cả khi RMS dưới 30 mA.
- Chặn đường nhanh trong cửa sổ đầu, nhưng vẫn dùng RMS cửa sổ đầu để kiểm
  tra bảo vệ. Chuyển tiếp nguồn/bias ở cửa sổ này vẫn có thể chốt lỗi RMS.
- Không có Vin dẫn tới UV_IN (0x40); thêm LEAK (0x08) thành 0x48. Thiếu AC
  tự nó không sinh LEAK. Vin=0 cũng không được dùng để bỏ qua bảo vệ rò.

## Mô phỏng với mã production

Chạy từ thư mục firmware:

```powershell
node Core/Measurement/tests/run_leakage_idle_audit.mjs C:/mingw64/bin/gcc.exe
```

Runner dùng Measurement, Protection, Rte, Calib và scheduler thực, thay ADC,
Flash và GPIO bằng stub. Gain và ngưỡng mặc định; Vin=Vout=0 trong mọi ca.

| Dữ liệu PA3 mô phỏng | RMS cuối [mA] | RMS lớn nhất đã công bố [mA] | Cờ nhanh | Fault cuối |
|---|---:|---:|---:|---|
| DC ổn định 1,6403 V | 0 | 0 | 0 | 0x40 |
| DC + nhiễu ±3 count | 12,599 | 12,599 | 0 | 0x40 |
| DC + 5,376 count RMS, đỉnh ±8 count | 22,577 | 22,577 | 1 | 0x48 |
| Chỉ một mẫu lệch 8 count sau startup | 0 | 0,751 | 1 | 0x48 |
| Bias dịch 48 count trong 50 ms đầu | 0 | 53,277 | 0 | 0x48 |
| Bias bước 12 count sau 210 ms | 0 | 10,984 | 1 | 0x48 |

Các dạng nhiễu/bước bias trên được đưa vào để kiểm tra độ nhạy thuật toán;
không khẳng định waveform thực của board giống các dạng mô phỏng. Ca thứ ba
tái hiện số RMS, offset, cờ và mask người dùng quan sát. Các ca còn lại giải
thích vì sao lỗi vẫn còn sau khi số đo đã giảm.

## Việc cần xác minh trên board

1. Khi chỉ cấp 12 V, đo PA3/I_LEAK_ADC, +1V65_REF và +3V3 so với AGND bằng
   oscilloscope, dùng dây mass ngắn. So sánh gợn/xung với mức vài mV ở trên.
2. C31/C32/C33 đã được xác nhận đều 10 nF (tổng cục bộ 30 nF) ở đầu ra
   U5 LM1117-3.3. Cần bổ sung tụ đầu ra sát OUT/GND theo đúng mã IC/nhà
   sản xuất. TI LM1117 mục 9.2.2.1.3 yêu cầu ít nhất 10 µF khi dùng tantalum,
   ESR 0,3..22 Ω. Các tụ nhỏ không thay thế tụ ổn định này. Đây là điểm
   phần cứng cần xử lý trước khi đánh giá lại ngưỡng; chưa có waveform để
   kết luận U5 là nguồn nhiễu duy nhất. Kiểm tra thêm tụ đầu vào và bố trí mass.
3. Kiểm tra AGND, đường bias, linh kiện khuếch đại, tải CT và dây CT; đối
   chiếu chân 1/2 là thứ cấp, chân 3/4 để hở theo datasheet.
4. Sau khi kiểm tra nguồn/nhiễu, reset và theo dõi khi nào cờ nhanh xuất hiện.
   Đọc thêm leakage_gain, ngưỡng RMS và ngưỡng nhanh ở cột Đang chạy.

Nguồn LM1117: https://www.ti.com/lit/ds/symlink/lm1117.pdf

Sau khi xử lý nguồn, reset MCU để xóa LEAK đã chốt. Khi vẫn chưa cấp Vin,
UV_IN=0x40 tiếp tục báo là đúng; cần theo dõi riêng bit LEAK=0x08 và cờ nhanh.

## Hướng cải tiến phần mềm

Cần đánh giá băng thông lọc dòng rò, khởi động sau khi bias ổn định và chính
sách xác nhận đường nhanh bằng waveform thực. Bộ lọc/xác nhận sẽ ảnh hưởng
độ trễ và đáp ứng xung, nên phải thử cả tín hiệu rò thật, không chỉ nhiễu.
Không tăng ngưỡng, trừ một sàn nhiễu cố định hoặc tắt bảo vệ rò khi Vin=0
để làm mất báo lỗi. Audit này chỉ sửa thông tin định mức CT và bổ sung công
cụ chẩn đoán; chưa thay hành vi bảo vệ hoặc ngưỡng đã lưu của người dùng.

Kiểm chứng: audit sáu trường hợp ở trên chạy xong; Calib 1.638 kiểm tra,
Measurement 161, Protection 7.998 RELEASE/7.996 DEBUG, tích hợp 1.030 mỗi
mode đều qua. Debug/Release build không lỗi, còn 50 cảnh báo .su/.cyclo như
trước. Hai file .bin sau sửa thông tin định mức có cùng SHA-256 với bản trước
audit: 0F58F77FE3DA324388D53B2CE1165819DC99BB3E95B6C1797666183A172C9D4F.
Chưa thực hiện phép đo điện, sửa linh kiện hay nạp lại board của người dùng.
