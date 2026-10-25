# Measurement theo schematic Charger Protection v2

Phạm vi hiện tại: Measurement.c, Measurement.h, tài liệu/kiểm thử trong cùng thư mục và giao diện dữ liệu chung Core/Rte/Rte.h. Các component khác và cấu hình dự án không thay đổi. Debug/Release chỉ được cập nhật sản phẩm build.

## Đối chiếu mạch

| Kênh | Chân / thứ tự DMA | Mạch và hệ số danh định |
|---|---|---|
| Vin | PA0 / 0 | (R4+R8)=9,4 MΩ; R13=33 kΩ; C6=100 nF; U1B gain 1 |
| Vout | PA1 / 1 | (R3+R7)=9,4 MΩ; R12=33 kΩ; C7=100 nF; U1C gain 1 |
| Iin | PA2 / 2 | R11 || R14 || R15 = 0,05/3 Ω; khuếch đại vi sai gain 1 |
| Ileak | PA3 / 3 | ZMCT118F 5 A / 5 mA (1000:1); R28=4,7 kΩ; khuếch đại vi sai gain 1 |
| Temperature | PA4 / 4 | R35=10 kΩ kéo lên +3V3, RT1=NTC kéo xuống AGND |

Với VDDA=3,3 V và ADC 12 bit:

- ADC_LSB = 3,3/4096 V/count.
- Vin/Vout: hệ số DC trước bù RC = 0,23029785 V/count, thay cho 0,156.
- Dòng shunt: 0,04833984 A/count, thay cho 0,025.
- Hai nhánh áp có tau = (9,4 MΩ || 33 kΩ) × 100 nF = 3,28846 ms; tần số cắt khoảng 48,40 Hz. Tại 50 Hz, độ lợi tương đối khoảng 0,6955 và trễ pha 45,93 độ. Chỉ thay hệ số điện trở sẽ vẫn sai Vrms, P và PF.
- Đầu vào vi sai dòng rò tải CT với khoảng R26+R31=200 Ω. Do đó R tải hiệu dụng là 4700 || 200 = 191,8367 Ω ở tần số thấp. Với CT dòng thứ cấp lý tưởng 1000:1 và gain 1: 0,004199738 A/count. Không được chỉ lấy R28=4700 Ω để tính hệ số.

Mô hình tải CT giả định các cặp điện trở vi sai khớp, op-amp làm việc tuyến tính, nguồn bias có trở kháng thấp và CT là loại xuất dòng thụ động. Điện cảm từ hóa, sai số theo burden/tần số và độ tuyến tính ở dòng nhỏ của CT thực chưa được biết.

### Kiểm tra lại tải CT và LEAK_EFFECTIVE_R

Với schematic hiện tại: R28 mắc ngang hai đầu CT là 4,7 kΩ; R26 và R31 là
hai điện trở đầu vào 100 Ω; R24 nối chân (+) về Vref là 100 Ω; R33 hồi tiếp
từ output về chân (-) là 100 Ω. Các tụ được bỏ qua trong xấp xỉ tần số thấp.

Đặt hai đầu CT là A/B, điện áp hai chân op-amp là P/N. CT thứ cấp để nổi,
nên dòng đi từ A vào R26 bằng dòng đi từ R31 về B, gọi là i. Khi hồi tiếp âm
hoạt động tuyến tính, P ≈ N, nhưng hai chân không có dây nối hoặc dòng chạy
xuyên qua op-amp từ chân (+) sang chân (-).

    A - P = i × R26
    N - B = i × R31
    A - B = i × (R26 + R31) = i × 200 Ω
    I_secondary = (A - B)/R28 + i
    R_effective = (A - B)/I_secondary = 4700 || 200 = 191,8367347 Ω

R24/R26 = R33/R31 = 1 cho `Vout - Vref = A - B`. Dòng không đi vào chân
op-amp, nhưng mạng điện trở và hồi tiếp vẫn tải CT. Vì vậy không thể coi
toàn khối khuếch đại có trở vào vô cùng lớn và chỉ dùng R28. 191,84 Ω là
tải vi sai tương đương lúc hoạt động, không phải kết quả đo ohm khi tắt nguồn.

Kiểm chứng độc lập bằng phương trình nút với I_secondary=30 µA và Vref=1,65 V:

| Nút / nhánh | Kết quả lý tưởng |
|---|---:|
| CT_POS | 1,655755102 V |
| CT_NEG | 1,650000000 V |
| Hai chân (+)/(-) | 1,652877551 V |
| Output trước R29 | 1,655755102 V |
| Dòng qua R28 | 1,224489796 µA |
| Dòng nhánh R26/R31 | 28,775510204 µA |

30 µA thứ cấp tương ứng 30 mA sơ cấp, tạo 5,755 mV RMS và khoảng 7,143
count RMS ở ADC với Vref ADC 3,3 V/gain=1. Thay 191,84 bằng 4700 trong code
sẽ làm số đo dòng nhỏ đi 24,5 lần. Công thức runtime được giữ nguyên.

Giả thiết trên không còn áp dụng nguyên dạng nếu một đầu CT bị nối thêm
GND/Vref bên ngoài, giá trị điện trở thực khác schematic, hoặc op-amp bão hòa.
Tụ và op-amp thực cần xét bằng trở kháng/đáp ứng tần số khi đánh giá nhiễu.
Tham khảo nguyên lý mạng khuếch đại vi sai và ảnh hưởng của điện trở/tụ:
[Analog Devices — A Deeper Look into Difference Amplifiers](https://www.analog.com/en/resources/analog-dialogue/articles/deeper-look-into-difference-amplifiers.html).

## Xử lý tín hiệu

### RMS dùng offset thực theo volt — cập nhật 2026-10-25

Đường RMS/công suất đã bỏ phép trừ midpoint cố định 2048. Bộ lọc và bộ tích
lũy nhận raw ADC; trong main, mỗi kênh tính riêng offset thực theo volt từ
chính cửa sổ đang xử lý. Đây cũng chính là giá trị được đưa vào các trường
`V_in_DC_Offset`, `V_out_DC_Offset`, `I_in_DC_Offset`, `I_leak_DC_Offset`.

    offset_v[ch] = mean(raw_adc[ch]) * ADC_LSB_V
    rms_adc_v[ch] = sqrt(mean((filtered_adc_v[ch] - offset_v[ch])^2))
    rms_real[ch] = rms_adc_v[ch] * hardware_gain_per_volt[ch] * calib_gain[ch]

Vin/Vout được bù RC và dòng tải được lọc/căn thời gian. Các kênh có LPF ADC
theo cập nhật bên dưới; RMS dòng rò dùng Ileak sau LPF. Công suất cũng dùng tích hai tín hiệu ADC theo volt đã
trừ offset tương ứng, sau đó đổi sang W; S/PF lấy từ các kết quả cùng cửa sổ.

Không cần thêm bộ đệm toàn bộ 2000 mẫu. DMA tích lũy các moment bằng số
nguyên Q8/Q16; main tính tương đương phép trừ offset volt từng mẫu. Trước
khi đổi moment sang float, chúng được dịch gần mức DC đo được của chính
cửa sổ để tránh mất độ chính xác khi lấy hiệu hai số bình phương lớn.
Mốc dịch số học này là giá trị động từ dữ liệu, không phải 2048. Kiểm thử
đối chiếu với chuyển từng mẫu sang volt và trừ offset trực tiếp bằng double.

ADC_LSB_V hiện vẫn dùng VDDA danh định 3,3 V, chưa tự đo VDDA bằng VREFINT.
Vì vậy offset hiển thị theo volt và các hệ số V/A còn phụ thuộc sai số VDDA.
ADC hardware calibration không tự đo VDDA hoặc bias của op-amp.

Đường fast leakage dùng offset riêng theo cập nhật bên dưới. `adc_offset[4]`
vẫn chứa trung bình raw làm tròn sau mỗi 200 ms cho đường thời gian cũ;
tần số/zero crossing dùng phần tử Vin. Các phần tử này không tham gia tính
RMS/công suất hoặc so ngưỡng dòng rò nhanh. Placeholder 2048 chỉ còn ảnh
hưởng tần số/crossing trước khi có trung bình thực. Zero crossing chờ cửa
sổ đầu; cập nhật trung bình vẫn chạy khi main chưa lấy cửa sổ pending.

| Đại lượng | Cách loại offset |
|---|---|
| RMS Vin/Vout/Iin/Ileak | Trừ offset volt thực của chính cửa sổ khỏi tín hiệu ADC theo volt |
| Công suất | Tích Vin_adc và Iin_adc sau khi trừ hai offset volt thực tương ứng |
| Dòng rò nhanh | Mẫu raw hiện tại trừ offset riêng có phần lẻ, học ban đầu rồi bám chậm theo mẫu |
| Tần số | Vin sau LPF ADC trừ offset Vin đã học; lúc đầu còn dùng placeholder |
| Zero crossing | Vin raw đã lọc trừ offset Vin đã học, cùng thang Q8 |
| Nhiệt độ | Lọc ADC NTC 10 Hz rồi lấy trung bình; không trừ 2048 |

Raw ổn định ở 2036 count tạo offset volt tương ứng và RMS=0. Offset RMS giữ
phần lẻ theo float volt, độc lập offset Q19 của đường nhanh.
Các cửa sổ vẫn là khối 2000 mẫu, 200 ms, không chồng lấn; chưa đổi sang
cửa sổ trượt. Raw ADC vẫn được giữ cho offset và kiểm tra clipping; đường
nhanh dùng Ileak sau LPF, giữ ngưỡng count cùng cơ chế giữ cờ tới reset.

Giới hạn cần giữ rõ: offset này loại DC trung bình, không loại được nhiễu
AC trên nguồn bias. Đường nhanh có thể chốt lỗi khi bias dịch quá nhanh.
RMS cửa sổ đầu vẫn bao gồm chuyển tiếp lúc nguồn/bias
đang ổn định; startup_complete chỉ báo có cửa sổ, không xác nhận bias đã
hết dao động. ADC hardware calibration cũng không đo bias của op-amp.

Kiểm chứng bằng mã production: 707 kiểm tra Measurement qua, gồm DC lệch
riêng bốn kênh (2036 và các mức 1200..2800 count), DC thuần cho RMS=0,
moment có trung bình 2035,5 count cho đúng 0,5 count RMS trước LPF; nhiễu
xen kẽ 2035/2036 bị LPF loại, cùng kiểm tra hai chiều ngưỡng nhanh
quanh 2036, và sóng AC với bốn offset riêng vẫn giữ RMS/P/tần số/crossing.
Thêm đối chiếu mean-square/công suất với phép tính volt từng mẫu, thử offset
volt khác trung bình để bảo đảm offset thực sự được sử dụng, và tín hiệu
0,5 count RMS trên nền DC 100..4000 count. Protection 8.007 RELEASE/8.005
DEBUG và tích hợp 1.037 RELEASE/1.030 DEBUG đều qua. Audit sáu tình huống
không AC đã chạy lại; nhiễu/xung ngắn có thể không còn kích fast sau LPF.

### Xác nhận fast leakage N=20 — firmware 2.7

Firmware 2.7 công bố thêm `MeasureResult.leakage_over_count_max` để đọc qua
ST-Link và UART (tool 1.7, tab Debug, đơn vị mẫu). Main sao chép từ bộ đếm
MAX của ISR trước khi kiểm tra cửa sổ RMS mới, không phải chờ đủ 200 ms.
Trường thêm ở cuối struct (offset 76, tổng 80 byte), các trường cũ giữ vị trí.
Giá trị giữ đến Reset MCU; không ghi Flash và không đổi logic N=20.

Để quan sát bằng Live Expressions, có hai biến file-static volatile:

- `leakage_over_count`: số mẫu vượt liên tiếp tại thời điểm đang đọc.
- `leakage_over_count_max`: chuỗi dài nhất đã thấy trước khi chốt fast,
  giữ tới Reset MCU (0..20). Ví dụ max=2 và count=0 nghĩa đã có chuỗi 2
  mẫu nhưng đã bị một mẫu dưới ngưỡng hủy; không phải bộ đếm không chạy.

Mẫu ADC cách 100 us và DMA xử lý 10 mẫu mỗi lượt. Count có thể tăng rồi
về 0 ngay trong một lượt DMA, nên Live Expressions có thể bỏ lỡ. Assembly
bản 2.5 đã có lệnh ghi RAM khi tăng/reset; chưa thấy lỗi bỏ tăng ở N=20.
Đánh dấu volatile để bảo đảm khả năng quan sát trong các build sau; biến
max mới giúp quan sát chắc chắn chuỗi ngắn. N=10/20 cùng logic tăng cho các
mẫu đầu, chỉ khác điểm xác nhận. Dùng ELF tương ứng đúng với HEX đã nạp.
`fast_leakage_ready=0` giữ count=0; sau khi fast đã chốt thì count giữ 20.

`MEASUREMENT_LEAK_FAST_CONFIRM_SAMPLES = 20` trong Measurement.h. Mỗi mẫu
Ileak sau LPF có `magnitude >= ngưỡng` tăng bộ đếm, mẫu dưới ngưỡng đưa về
0. Đủ 20 mới chốt cờ. Bộ đếm giữ qua HT/TC DMA, các lần main chạy và biên
cửa sổ RMS 200 ms; xóa khi khởi tạo hoặc offset chưa sẵn sàng. Đếm theo
mẫu ADC 100 us, không theo lần ngắt 1 ms. Offset không cập nhật trong chuỗi
đang vượt ngưỡng, kể cả trước lúc đủ 20. RMS và các ngưỡng lưu Flash giữ nguyên.

Từ mẫu vượt đầu đến mẫu thứ 20 cách 1,9 ms; chưa gồm trễ LPF, gom DMA và
relay. Bước raw 12 count trước đây vượt LPF ở mẫu thứ 3; nay xác nhận ở mẫu
thứ 22. Trong test, một xung raw 50 count tạo khoảng 3 mẫu sau lọc vượt
ngưỡng, 100 count tạo khoảng 4 mẫu: đều không chốt fast. Xung lớn hơn có đủ
20 mẫu vẫn chốt. Dòng rò dạng xung ngắn cũng bị ảnh hưởng bởi điều kiện này;
chưa xác minh đáp ứng bảo vệ trên board.

`FastLeakTrip` ghi mẫu thứ 20 xác nhận, không phải mẫu đầu của một chuỗi
chưa được xác nhận. `I_leak_peak_max` vẫn ghi mọi đỉnh từ khi offset sẵn
sàng: MAX vượt ngưỡng mà fast=0/trace.valid=0 là phù hợp nếu chưa đủ chuỗi.
Tool 1.4 và UART giữ nguyên; INFO báo firmware 2.6 cho bản có biến max của bộ đếm.

707 kiểm tra Measurement qua, gồm 1..19/20 mẫu, gián đoạn nhiều lần, hai cực,
ngưỡng bằng chính xác, giữ offset, giữ MAX, qua biên DMA/RMS và reset.
Tích hợp với Protection/ADC thật kiểm tra không yêu cầu relay trip khi mới
có 3 mẫu, và yêu cầu đúng khi đủ 20. Xem FLASH_OPTIMIZATION.md để biết bộ nhớ.

### Low-pass ADC — cập nhật 2026-10-25

| Kênh | Bộ lọc trước xử lý | Các bước tiếp theo |
|---|---|---|
| Vin/Vout | LPF ADC 1 kHz | Bù RC có giới hạn băng thông 1 kHz, RMS/crossing |
| Iin | LPF ADC 1 kHz | Căn lệch thời gian lấy mẫu và LPF 1 kHz, RMS/công suất |
| Ileak | LPF ADC 1 kHz | Cùng tín hiệu cho RMS và fast leakage |
| NTC | LPF ADC 10 Hz | Trung bình cửa sổ 200 ms rồi đổi nhiệt độ |

LPF ADC là IIR bậc một từ biến đổi bilinear/Tustin. `FilterAdcSample()` dùng
trạng thái Q16 và trả Q8; khởi tạo từ mẫu thực đầu tiên. Hệ số tính khi
biên dịch, không thêm float trong vòng lấy mẫu. Tần số cắt danh định nằm
trong `Measurement.h`: `MEASUREMENT_BANDWIDTH_HZ` cho Vin/Vout/Iin,
`MEASUREMENT_LEAK_BANDWIDTH_HZ` và `MEASUREMENT_TEMP_BANDWIDTH_HZ`.

Bộ bù RC điện áp cũ có thể khuếch đại nhiễu ADC cao tần; LPF mới đặt trước
bộ bù để hạn chế phần này. Iin được thêm cùng một pole để giữ khớp pha khi
tính công suất. Toàn chuỗi Vin/Vout/Iin có hai pole khớp nhau: suy giảm biên
độ danh định tại 50 Hz khoảng 0,25%, công suất khoảng 0,50%; tại 60 Hz lần
lượt khoảng 0,36% và 0,72%. Không tự đổi gain hoặc Calib đã lưu để che sự
suy giảm này. Ileak chỉ có một pole 1 kHz, suy giảm khoảng 0,125% tại 50 Hz.

Fast so ngưỡng trên tín hiệu đã lọc và xác nhận 20 mẫu như phần trên, không
giữ đường raw song song để chốt mọi spike. Kiểm thử hai cực tính: một spike
8 count hoặc 100 count dài 100 us không chốt; bước 12 count chốt ở mẫu
thứ 22 (~2,2 ms theo chu kỳ lấy mẫu). Đây là thời gian DSP trong test, chưa cộng
gom DMA tối đa khoảng 1 ms, chờ zero crossing và cơ khí relay. Xung thật
quá ngắn cũng bị suy giảm; không suy ra đáp ứng bảo vệ xung trên board từ
việc hết báo nhiễu. Kiểm thử zero crossing 50/60 Hz cho sai lệch <=500 us.

Clipping vẫn lấy trực tiếp raw ADC. NTC còn kiểm tra trung bình raw theo
biên hở/ngắn mạch trước khi dùng kết quả LPF, nên chẩn đoán nguyên cửa sổ
không phải chờ lọc ổn định. Bình thường `temperature_adc_average` là trung
bình ADC đã lọc; khi raw NTC không hợp lệ, trường này giữ raw mean để báo
lỗi. LPF 10 Hz có tau khoảng 15,9 ms; thời điểm vượt ngưỡng nhiệt còn phụ
thuộc cửa sổ 200 ms. Test bước 25->85 C vượt 70 C ở cửa sổ đầu và ổn định
ở cửa sổ sau. Các bước gần ngưỡng có thể bị trễ thêm một cửa sổ.

Kiểm thử độc lập đáp ứng 1..4500 Hz, nhiễu xen kẽ trên cả năm kênh, bước/
xung, NTC lỗi và raw clipping đều qua. Trong audit nhiễu 5,376 count RMS,
đỉnh 8 count: số đo sau lọc còn 0,983 count (4,128 mA), fast=0 thay vì 1;
đây là mẫu mô phỏng, không phải waveform đã đo trên board. Lọc số không
loại được mọi nhiễu hoặc alias đã có trước ADC. Giao thức UART không đổi.

### Offset cho fast leakage — cập nhật 2026-10-25

`ProcessFastLeakage()` dùng `leakage_offset_q19`, đơn vị count nhân 524288.
Không có midpoint 2048 trong phép so ngưỡng này và không dùng offset RMS
cùng cửa sổ, vì đường nhanh phải quyết định trước khi có toàn bộ cửa sổ.

- Học ban đầu: chỉ nhận cửa sổ 200 ms không clipping Ileak, với chênh lệch
  trung bình hai nửa 100 ms không quá 1 count. Nếu chưa đạt, thử lại cửa sổ
  kế tiếp. Seed lấy từ trung bình thực, độ phân giải 1/256 count, lưu ở Q19.
- Khi đã sẵn sàng, so `abs(filtered_q8 * 2048 - leakage_offset_q19)` với ngưỡng
  count cũ nhân 524288, trước mọi cập nhật offset. So cả hai cực tính; một
  chuỗi 20 mẫu sau lọc đủ ngưỡng mới chốt cờ. Ngưỡng lưu Flash và cách lượng tử hóa không đổi.
- Nếu chưa có lỗi, cập nhật theo mỗi mẫu: `offset += error / 8192`.
  Tại 10 kHz, hằng số thời gian xấp xỉ 0,8192 s. Không phải cửa sổ trượt.
  Phép chia số nguyên làm tròn về 0 đối xứng; vùng không cập nhật do lượng
  tử nhỏ hơn 1/64 count. Không có float hoặc bộ đệm bổ sung theo mẫu.
- Cờ fast đã chốt hoặc RMS Ileak vượt ngưỡng sẽ giữ offset tới reset.
  Giữ offset không tắt so ngưỡng fast. Clipping Ileak tạm dừng học trong
  phần còn lại của cửa sổ đó; cửa sổ sạch sau đó có thể tiếp tục bám.
- `MeasureResult.fast_leakage_ready` được DMA đặt khi đã có seed hợp lệ,
  dùng byte padding cũ, không dời các trường cũ; struct hiện 80 byte sau
  khi thêm hai giá trị MAX và MAX số mẫu cuối struct. Protection chỉ cho đóng/
  phục hồi relay khi cờ này và `startup_complete` đều bằng 1. RMS và các
  chẩn đoán vẫn được công bố, kiểm tra ngay cửa sổ đầu dù bias chưa sẵn sàng.

Watch: `leakage_offset_q19 / 524288.0` là offset fast theo count; nhân thêm
`3.3 / 4096` để xem volt. `I_leak_DC_Offset` trên PC vẫn là offset của RMS,
không phải trạng thái bám fast. UART 72 byte hiện tại không thêm trường.

### Bản chụp fast trip đầu tiên

MAX hiện có trong Watch và tool PC 1.4:

- `MeasureResult.I_leak_peak_max` [A]: đỉnh lớn nhất của
  `abs(Ileak_sau_LPF - offset_fast)`, tính trên từng mẫu sau khi offset đã
  sẵn sàng. ISR giữ max số nguyên Q19; main xuất float khi có kỷ lục mới,
  kể cả lúc chưa đủ cửa sổ RMS. Không thêm float trên mẫu khỏe trong ISR.
- `MeasureResult.I_leak_rms_max` [A]: RMS lớn nhất từ cửa sổ đầu, gồm cả
  chuyển tiếp nguồn lúc khởi động. RMS hiện tại thấp không xóa giá trị này.

Cả hai giữ tới Reset MCU, tiếp tục cập nhật sau lỗi và không ghi Flash.
Đỉnh dùng offset fast nên có thể bao gồm trôi bias, không chỉ dòng rò vật
lý. `FastLeakTrip` bên dưới vẫn giữ mẫu đầu tiên, độc lập MAX có thể tăng
sau đó. Tool chuyển A sang mA; đọc lệnh mới 0C theo capability, giữ MEAS cũ.
Struct MeasureResult hiện 80 byte, các trường cũ không đổi vị trí. Kiểm
tra hiện tại: Measurement 709; UART 19 bài và GUI 7 bài đã qua ở bản tool 1.7; có ca bắt
đỉnh giữa polling, cả hai cực tính, max sau lỗi/reset/gain và firmware cũ.

Trong Watch, mở biến global volatile `FastLeakTrip`. Khi `valid=1`, các
trường là dữ liệu đúng mẫu đầu tiên đặt `leakage_trip_flag`, giữ tới reset:

| Trường | Ý nghĩa |
|---|---|
| `raw_adc_count` | ADC Ileak thô của mẫu kích lỗi |
| `filtered_adc_count` | ADC sau LPF, gồm cả mức DC, đơn vị count |
| `offset_adc_count` | Offset fast đã dùng ở mẫu đó, có phần lẻ |
| `delta_adc_count` | Độ lệch có dấu sau lọc trừ offset, tính trực tiếp từ số nguyên so ngưỡng |
| `threshold_adc_count` | Ngưỡng count thực tế, đã gồm cấu hình và gain đang chạy |
| `sample_index` | Chỉ số mẫu từ khởi động; tại 10 kHz chia 10 để lấy ms |

`abs(delta_adc_count)` đạt ngưỡng chứng minh vì sao fast đã chốt, dù RMS
200 ms hiện tại thấp. Một mẫu không đủ xác định nguồn vật lý của nhiễu hay
phân biệt mọi bước bias với dòng rò thật. Snapshot giúp kiểm tra giá trị so
ngưỡng; không đổi cutoff, ngưỡng, logic latch hay tự xóa lỗi. Chuyển sang
float chỉ làm một lần sau khi đã đặt cờ, không thêm float trên mẫu khỏe.
`valid` được ghi cuối cùng. Snapshot chỉ có trong Watch, không thêm UART.

Đã thêm kiểm tra raw/filter/offset/ngưỡng/thời điểm đầu tiên, cả hai cực
tính, không bị mẫu sau ghi đè và xóa khi khởi tạo. Tổng Measurement hiện
tại 707 kiểm tra qua; Protection/tích hợp/Calib qua với N=20; UART đã qua ở bản trước.

Các ca kiểm tra mới bao gồm bias khởi động chưa ổn định, clipping, offset
2035,5 count, cả hai cực tính, trôi 2 count/s khi main không lấy snapshot,
đáp ứng thời gian, sin 50/60 Hz, xung nửa chu kỳ, bước nhảy và đóng băng
sau lỗi fast/RMS. Chưa xác nhận đáp ứng trên board. Bộ bám DC không phân
biệt được mọi dạng rò chậm/bất đối xứng với trôi analog; không suy ra khả
năng bảo vệ DC từ thuật toán hoặc CT này. Nhiễu AC/đỉnh nhiễu không được
loại bỏ bằng việc sửa offset. RMS vẫn bao gồm chuyển tiếp khởi động.

### Chuỗi xử lý

- Giữ tốc độ hiện có 10 kHz/kênh, bộ đệm DMA 100 phần tử, xử lý 10 mẫu/kênh mỗi lần ngắt và cửa sổ 2000 mẫu/kênh (200 ms).
- Khôi phục áp bằng bộ lọc inverse RC có giới hạn nhiễu: (1+s*tau_divider)/(1+s*tau_bandwidth). Dòng shunt dùng bộ lọc 1/(1+s*tau_bandwidth) để hai đường có pha tương ứng.
- Trước đó, LPF ADC 1 kHz áp dụng đồng bộ Vin/Vout/Iin; Ileak có LPF 1 kHz cho RMS/fast, NTC có LPF 10 Hz. Clipping và offset cửa sổ vẫn lấy raw.
- Băng thông thiết kế mặc định 1 kHz; bộ lọc được triển khai fixed point, hệ số Q16 và tín hiệu Q8. Không dùng phép toán float cho các bộ lọc trong vòng lặp mẫu.
- RMS và công suất là các đại lượng có giới hạn băng thông. Với sóng 50 Hz, bộ lọc chung còn suy hao khoảng 0,125% biên độ và 0,25% công suất; không tự bù bằng một hệ số 50 Hz vì làm vậy sẽ sai tương quan các hài. Mô hình số là xấp xỉ Tustin, chính xác tốt tại 50/60 Hz và các hài thấp; không phải máy đo toàn dải đến Nyquist.
- Nội suy dòng về thời điểm lấy mẫu Vin để bù độ trễ hai lần chuyển đổi ADC: khoảng 7,714 microsecond, theo clock ADC 14 MHz và 54 chu kỳ/kênh đang cấu hình.
- Tính offset ADC thực theo volt của từng kênh trong cùng cửa sổ; RMS/công suất dùng tín hiệu ADC theo volt đã trừ offset tương ứng. Không trừ midpoint 2048 trong đường RMS.
- Đo tần số dùng qua zero có nội suy, xác nhận bằng hysteresis, giới hạn 40–70 Hz và timeout 100 ms nếu không có chu kỳ hợp lệ. Không khởi tạo một tần số 50 Hz giả khi chưa có tín hiệu.
- Bộ lọc số sau ADC không ngăn được alias đã xuất hiện khi lấy mẫu. Sai số linh kiện, Vref, pha cảm biến và giới hạn tuyến tính analog vẫn cần được kiểm tra trên board. Đặc biệt op-amp LMV324 không có dải common-mode đầu vào tới sát rail dương; clipping hoặc làm việc ngoài dải tuyến tính không thể khôi phục bằng code. Xem datasheet TI: https://www.ti.com/lit/ds/symlink/lmv324.pdf

## Thông số cảm biến do người dùng xác nhận ngày 2026-10-21

### RT1: TDK NTCG163JF103HT1

R25=10 kΩ ±3%, B25/85=3435 K ±1%, nhiệt độ hoạt động -40..125 °C theo TDK:
https://product.tdk.com/system/files/dam/doc/product/sensor/ntc/chip-ntc-thermistor/data_sheet/datasheet_ntcg163jf103ht1.pdf

TDK còn ghi B25/50=3380 K. Code dùng nhất quán B25/85=3435 K trong mô hình Beta hiện tại để giữ Flash nhỏ; đây không phải bảng R/T chính xác trên toàn dải.

Các macro cần thay trong Measurement.h:

- MEASUREMENT_NTC_PULLUP_OHM: điện trở R35 thực tế, hiện 10000.
- MEASUREMENT_NTC_R25_OHM: điện trở NTC tại nhiệt độ tham chiếu, hiện 10000.
- MEASUREMENT_NTC_BETA_K: Beta của NTC thực, hiện 3435.
- MEASUREMENT_NTC_T0_K: nhiệt độ tham chiếu theo Kelvin, hiện 298,15 K = 25 °C.

Công thức hiện tại, đúng với RT1 nằm phía AGND:

    Rntc = R35 × ADC_average / (4096 - ADC_average)
    T_K  = 1 / (1/T0 + ln(Rntc/R25)/Beta)
    T_C  = T_K - 273,15

Do cầu chia và ADC cùng tham chiếu nguồn +3V3, phép đo nhiệt độ là ratiometric. ADC khoảng 2048 cho 25 °C với NTC 10 kΩ, không phải 164,9 °C như công thức tuyến tính cũ.

Beta là mô hình xấp xỉ. Khi cần độ chính xác cao trên dải rộng, dùng bảng R/T hoặc hệ số Steinhart-Hart của NTC thực trong ConvertTemperature(). Nếu RT1 thực không phải NTC, công thức này cần thay.

NTC gần chạm 0 hoặc 4096 count được đánh dấu lỗi; Temperature trả về NAN, không báo một nhiệt độ hợp lệ giả. Đây là phát hiện giá trị gần rail, không tự phân biệt được mọi nguyên nhân lỗi mạch.

### Dòng rò: ZMCT118F, 5 A / 5 mA = 1000:1

Datasheet người dùng cung cấp ngày 2026-10-24 ghi định mức 5 A / 5 mA,
tỷ số 1000:1 và dải tuyến tính 0..30 A với tải 50 Ω. Hai macro dòng định mức
được sửa thành 5 và 0,005 A; tỷ số vẫn là 1000 như khai báo 30/0,030 trước
đó, nên thang đo không đổi. Dòng thứ cấp định mức không phải ngưỡng dòng rò.

Nguồn nhà sản xuất:
https://www.micro-transformer.com/pcb-mounting-current-transformer-zmct118f.html

Trang hãng ghi 5 A / 5 mA và dải 0..30 A với tải 50 Ω. Tải hiệu dụng của schematic hiện tại khoảng 191,84 Ω; độ chính xác ở dòng rò cỡ mA với tải này chưa được nguồn đó xác nhận. Hệ số danh định cần được hiệu chuẩn trên board ở các mức dòng rò quan tâm.

Các macro cần thay trong Measurement.h:

- MEASUREMENT_LEAK_CT_PRIMARY_A: dòng sơ cấp định mức, hiện 5 A.
- MEASUREMENT_LEAK_CT_SECONDARY_A: dòng thứ cấp định mức, hiện 0,005 A.
- MEASUREMENT_LEAK_CT_RATIO: tự tính PRIMARY_A / SECONDARY_A = 1000; nếu đổi CT, thay hai giá trị trên.
- MEASUREMENT_LEAK_BURDEN_OHM: R28 thực, hiện 4700.
- MEASUREMENT_LEAK_R26_OHM và MEASUREMENT_LEAK_R31_OHM: đầu vào vi sai, hiện đều 100.
- MEASUREMENT_LEAK_GAIN: độ lợi vi sai, hiện 1.
- MEASUREMENT_LEAK_CALIB: hệ số hiệu chuẩn phần còn lại, hiện 1.

Nếu cảm biến thực là loại xuất điện áp/active, phải thay mô hình LEAK_A_PER_COUNT trong Measurement.c; chỉ đổi tỉ số CT sẽ không đủ.

Để hiệu chuẩn bằng dòng rò đã biết:

    trim_moi = trim_cu × I_tham_chieu / MeasureResult.I_leak_rms

Ghi trim_moi vào MEASUREMENT_LEAK_CALIB. Nên kiểm tra tại nhiều mức dòng; một hệ số đơn không sửa được CT phi tuyến hoặc bão hòa. MeasureResult.leakage_adc_rms cho biết RMS theo count để đối chiếu trực tiếp với oscilloscope/dữ liệu ADC.

Với cấu hình ZMCT118F hiện tại, 30 mA RMS chỉ khoảng 7,14 count RMS: độ phân giải hữu hạn và nhiễu của chuỗi analog có ảnh hưởng đáng kể. Chưa có số liệu thực nghiệm trên board để xác nhận sai số này.

## Cờ dòng rò và chẩn đoán

- MEASUREMENT_LEAK_FAST_PEAK_A = 0,030 A là ngưỡng tức thời/đỉnh được giữ theo ý nghĩa đường fast trước đây; KHÔNG phải ngưỡng RMS 30 mA.
- Ngưỡng được lượng tử thành 8 count (khoảng 33,6 mA tức thời), so với độ lệch có phần lẻ quanh offset fast. Sự khác biệt với 30 mA là do lượng tử hóa ngưỡng.
- Với sin, 20 mA RMS có đỉnh khoảng 28,3 mA; 30 mA RMS có đỉnh khoảng 42,4 mA.
- Cửa sổ 200 ms đầu tiên đạt kiểm tra ổn định/clipping sẽ xác lập bias dòng rò và bật fast; có thể cần nhiều cửa sổ. RMS dòng rò vẫn tính ngay cửa sổ đầu.
- leakage_trip_flag giữ latch; module không tự xóa và không điều khiển relay.
- MeasureResult.startup_complete: đã tính được ít nhất một cửa sổ.
- MeasureResult.temperature_fault: phép đổi nhiệt độ không hợp lệ, Temperature là NAN.
- MeasureResult.adc_clipped_mask: bit 0/1/2/3/4 lần lượt Vin/Vout/Iin/Ileak/Temp gần rail ADC trong cửa sổ vừa xử lý. Không có bit clipping cũng không đảm bảo op-amp luôn làm việc tuyến tính.

## Kiểm thử và build

Chạy bộ kiểm thử bằng Node.js và GCC native (MinGW trên Windows):

    node Core/Measurement/tests/run_measurement_tests.mjs "C:/Program Files (x86)/Dev-Cpp/MinGW64/bin/gcc.exe"

Có thể bỏ đối số compiler nếu gcc đã nằm trong PATH. Runner tạo artifact ở thư mục tạm, biên dịch chính Measurement.c với stub hardware. Các file .c.in và .h.in tránh bị CubeIDE nhận nhầm thành source firmware.

Kết quả ngày 2026-10-20: 99 kiểm tra, 0 lỗi. Bao gồm 50/60 Hz; PF 1, PF 0,5 sớm/trễ; sóng có hài bậc 3; bias DC; NTC 0/25/50/85 °C và hở/chập; mất AC và có AC trở lại; timestamp wrap; latch dòng rò; snapshot bận; ngắt DMA HT/TC/TE.

Ví dụ mô phỏng mạch lý tưởng ở 50 Hz, nguồn 220 V RMS / 5 A RMS / PF 1:

    Vin = 219,736 V; Iin = 4,9938 A; P = 1097,303 W; PF = 0,99999

Sai khác chủ yếu phù hợp với giới hạn băng thông số và lượng tử hóa. Đây là kiểm thử mô hình với mã C, không phải kết quả đo trên board.

Debug và Release đều build thành công, không có warning/error. Flash text+data = 9484 byte / 16384 byte (57,89%). RAM data+bss theo size = 2148 byte / 4096 byte (52,44%), trong đó đã gồm vùng heap/stack tối thiểu do linker dành trước. Chưa đo worst-case thời gian ISR hoặc thử trực tiếp trên phần cứng.

## Tối ưu Flash ngày 2026-10-20

Giữ nguyên API, tần số lấy mẫu, bộ đệm DMA, hệ số phần cứng, bù RC, băng thông, hysteresis, timeout, cờ dòng rò và mô hình NTC. Chỉ thay cách thực hiện phép toán trong Measurement.c; không sửa cờ compiler/linker hoặc component khác.

| Bộ nhớ toàn firmware | Trước | Sau | Tiết kiệm |
|---|---:|---:|---:|
| Flash (text+data) | 14256 byte / 87,01% | 9484 byte / 57,89% | 4772 byte |
| RAM theo size, gồm heap/stack tối thiểu | 2540 byte / 62,01% | 2148 byte / 52,44% | 392 byte |

Nguyên nhân lớn nhất là các phép ép int64_t/uint64_t sang float. Toolchain Cortex-M0 hiện tại triển khai chúng thông qua helper double, kéo thêm khoảng 3,8 KiB mã dù ứng dụng dùng float. Unsigned64ToFloat tách thành hai phần 32 bit; Signed64ToFloat đổi dấu bằng số nguyên không dấu, kể cả INT64_MIN. Kết quả có thể khác phép ép trực tiếp tối đa một ULP float. Giữ nguyên các bộ cộng dồn 64 bit để tránh tràn; không cắt dữ liệu mẫu để đổi lấy dung lượng.

MeasurementSqrt dùng seed theo exponent binary32 và bốn bước Newton. Hàm chỉ dùng cho phương sai bằng 0 hoặc số normal dương hữu hạn do module tạo ra; không phải thay thế tổng quát cho sqrtf với subnormal/NaN/infinity.

MeasurementLog chuẩn hóa mantissa rồi dùng chuỗi ln với bốn số hạng. Hàm chỉ nhận tỉ số điện trở NTC normal, dương, hữu hạn sau kiểm tra rail. NAN khi NTC lỗi vẫn giữ nguyên. Nếu đổi cấu hình cảm biến tới mức tạo tỉ số ngoài miền số normal hữu hạn, cần kiểm tra lại điều kiện này.

Bỏ phụ thuộc logf/sqrtf chuẩn giúp linker loại thêm mã libm và dữ liệu reentrancy/stdio mà các đường xử lý errno kéo theo. Sản phẩm build cuối không còn các helper double __aeabi_dadd/__aeabi_dmul hoặc symbol logf/sqrtf. Hàm math gọn có kiểm tra định dạng IEEE-754 binary32 ở thời điểm compile.

Kiểm thử bổ sung với chính mã C:

- 200020 phép chuyển số nguyên 64 bit, gồm giá trị biên và ngẫu nhiên: chênh lệch tối đa một ULP.
- 32512 giá trị căn bậc hai, bao phủ mọi exponent normal dương binary32: sai số tương đối lớn nhất khoảng 8,87e-8.
- Quét từng mã ADC NTC hợp lệ (5..4091): sai số tuyệt đối ln lớn nhất khoảng 2,57e-7; sai lệch nhiệt độ so với công thức tính bằng double lớn nhất khoảng 0,000076 °C. Đây là sai số tính toán, không phải độ chính xác cảm biến thực tế.
- Toàn bộ kiểm thử tín hiệu và chẩn đoán trước đó tiếp tục đạt.

Tổng cộng 99 kiểm tra tổng hợp, 0 lỗi. Debug/Release cùng kích thước và build không có cảnh báo. Giữ nguyên phần heap/stack tối thiểu 1536 byte của linker; không giảm vùng dự phòng để làm đẹp số liệu RAM. Padding căn chỉnh giữa các section có thể làm số liệu dùng RAM theo địa chỉ chênh vài byte so với tổng do size hiển thị.

## Xác nhận cập nhật cảm biến ngày 2026-10-21

- RT1 được xác nhận là TDK NTCG163JF103HT1: R25=10 kΩ, đổi B25/85 từ 3977 K sang 3435 K. Mô hình Beta và các phép toán gọn giữ nguyên.
- CT được xác nhận là ZMCT118F 30 A / 30 mA. Tỉ số 1000:1 không đổi; đã khai báo riêng MEASUREMENT_LEAK_CT_PRIMARY_A và MEASUREMENT_LEAK_CT_SECONDARY_A rồi suy ra MEASUREMENT_LEAK_CT_RATIO.
- Bộ tạo tín hiệu NTC trong kiểm thử đã cập nhật độc lập sang 3435 K; kiểm tra mô hình tại -40/0/25/50/85/125 °C, kèm quét mọi mã ADC hợp lệ.
- Kết quả hiện tại: 103 kiểm tra, 0 lỗi. Sai lệch tính toán NTC lớn nhất so với mô hình Beta double khoảng 0,000070 °C; con số này không bao gồm sai số mô hình Beta so với bảng R/T thực, dung sai NTC hay sai số mạch.
- Debug/Release đều build thành công, không có cảnh báo; Flash 9484 byte (57,89%), RAM theo size 2148 byte (52,44%). Chưa thử trực tiếp trên board.

## Hợp nhất dữ liệu vào Rte ngày 2026-10-21

Đã bỏ kiểu MeasurementDiagnostics_t và biến MeasurementDiagnostics riêng. Năm trường chẩn đoán nằm trực tiếp trong MeasureResult_t, dùng chung biến MeasureResult được định nghĩa duy nhất tại Rte.c:

| Trường | Ý nghĩa |
|---|---|
| startup_complete | Đã công bố ít nhất một cửa sổ hoàn chỉnh; không phải cờ dữ liệu mới |
| temperature_fault | NTC gần rail hoặc phép đổi không hợp lệ; Temperature là NAN |
| leakage_adc_rms | RMS AC dòng rò theo ADC count, trước khi đổi sang ampere |
| temperature_adc_average | ADC count trung bình của NTC |
| adc_clipped_mask | Các kênh có mẫu gần rail trong cửa sổ vừa công bố |

Rte.h định nghĩa AdcClippedMask_t và các cờ ADC_CLIPPED_VAC_IN, ADC_CLIPPED_VAC_OUT, ADC_CLIPPED_IAC, ADC_CLIPPED_ILEAK, ADC_CLIPPED_TEMP tương ứng bit 0..4. ADC_CLIPPED_NONE = 0; ADC_CLIPPED_ALL chứa cả năm bit. Một cửa sổ có thể bật nhiều bit đồng thời. Đây là cờ phát hiện gần rail ADC, không phát hiện được mọi dạng méo/bão hòa trước ADC.

Module sử dụng chỉ cần include Rte.h. Ví dụ trong main loop, sau khi Measurement_MainFunction() trả về:

    #include "Rte.h"

    if (MeasureResult.startup_complete &&
        (MeasureResult.adc_clipped_mask & ADC_CLIPPED_ILEAK) != ADC_CLIPPED_NONE)
    {
        /* Kenh dong ro co mau gan rail trong cua so vua xu ly. */
    }

Khi chuyển code cũ, thay MeasurementDiagnostics.<trường> bằng MeasureResult.<trường>. Không còn biến/kiểu chẩn đoán riêng. Measurement có kiểm tra compile-time để bảo đảm các bit công khai khớp thứ tự quét ADC.

Các trường được cập nhật trong Measurement_MainFunction(); đọc từ main loop sau lời gọi này sẽ có kết quả và chẩn đoán cùng cửa sổ. Struct không cung cấp snapshot nguyên tử cho ISR/task đọc đồng thời. leakage_trip_flag vẫn là cờ volatile được latch độc lập từ DMA ISR. Không dùng startup_complete làm cờ đồng bộ hoặc phát hiện dữ liệu còn mới.

Kiểm chứng: 132 kiểm tra, 0 lỗi, bao gồm từng bit clipping, tổ hợp nhiều bit và một consumer chỉ include Rte.h, liên kết với Rte.c thật. Debug/Release build thành công, không có warning/error. MeasureResult chiếm 68 byte, thay cho tổng 72 byte của hai biến trước đây; do căn chỉnh section, tổng RAM firmware theo size vẫn là 2148 byte (52,44%). Flash vẫn 9484 byte (57,89%). Chưa thử trên board.

## Ngưỡng dòng rò nhanh lưu Flash

MeasurementInit lấy leak_fast_peak_a từ Rte_Call_Calib_GetProtection(), kết hợp leakage_gain để tính ngưỡng ADC. Chưa cấu hình thì dùng 0,030 A đỉnh như trước. Tham số chỉ áp dụng sau reset; ISR vẫn chỉ so sánh số nguyên. Ngưỡng RMS riêng nằm trong cấu hình Protection. Xem Core/Protection/README.md.
