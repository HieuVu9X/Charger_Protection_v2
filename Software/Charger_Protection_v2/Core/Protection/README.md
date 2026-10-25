# Protection

Đóng hoặc phục hồi relay cần cả `startup_complete` và `fast_leakage_ready`.
Cờ thứ hai xác nhận đường nhanh đã học được offset dòng rò ổn định. RMS và
các chẩn đoán vẫn được kiểm tra khi đã có cửa sổ đo, dù offset fast chưa sẵn
sàng; điều kiện mới không bỏ qua lỗi RMS lúc khởi động.

## Ngưỡng lưu Flash

Ngưỡng được đọc từ cùng record Flash với Calib khi `Calib_Init()` chạy trước
`ProtectionInit()`/`MeasurementInit()`. Chưa lưu, record lỗi hoặc record Calib
v1 cũ thì dùng toàn bộ ngưỡng mặc định dưới đây. Không ghi Flash khi boot.

| Tham số | Mặc định | Đơn vị |
|---|---:|---|
| Quá áp Vin và Vout (`ov_v`) | 240 | V RMS |
| Thấp áp Vin (`uv_in_v`) | 190 | V RMS |
| Quá dòng (`oc_a`) | 2,8 | A RMS |
| Dòng rò (`leak_a`) | 30 | mA RMS |
| Quá nhiệt (`ot_c`) | 70 | °C |
| Phục hồi quá áp (`ov_recover_v`) | 230 | V RMS |
| Phục hồi thấp áp (`uv_in_recover_v`) | 200 | V RMS |
| Phục hồi nhiệt (`ot_recover_c`) | 65 | °C |
| Dòng rò nhanh (`leak_fast_peak_a`) | 30 | mA tức thời/đỉnh |
| Giữ ổn định để phục hồi (`recover_hold_ms`) | 1000 | ms |

Mở tool PC → tab **Ngưỡng Protection** → sửa **Giá trị mới** → **Lưu ngưỡng
vào Flash** → **Reset MCU / áp dụng**. Cột Đang chạy và Trong Flash phân biệt
bộ đang dùng và bộ chờ reset. Nút **Khôi phục ngưỡng mặc định** chỉ khôi phục
Protection; Calib được giữ nguyên. Nút mặc định Calib cũng giữ nguyên Protection.
Lưu thay đổi mở relay, dừng ADC/DMA tới khi reset như SAVE_CALIB hiện có.
Không có thay đổi thì không ghi thêm. Tool cũ vẫn dùng được các lệnh Calib.

API: `Calib_GetProtection`, `Calib_ReadStoredProtection`,
`Calib_SaveProtection`, `Calib_RestoreProtectionDefaults`; client firmware dùng
port tương ứng trong Rte.h. Kiểu dữ liệu và mặc định ở
`Core/Calib/ProtectionParams.h`. Các macro `PROTECTION_*` là mặc định factory;
logic runtime dùng bộ đã kiểm tra hợp lệ từ Calib. Các khoảng chấp nhận là
giới hạn phần mềm, cần chọn ngưỡng phù hợp board và tải thực tế.

Validation: `1 <= UV < UV phục hồi < OV phục hồi < OV <= 400 V`;
OC 0,01..30 A; hai ngưỡng rò 1..1000 mA độc lập;
`-40 <= nhiệt phục hồi < nhiệt cắt <= 150 °C`; thời gian nguyên 1..65535 ms.
NaN/Inf và bộ hysteresis sai bị từ chối trước khi vào chế độ ghi Flash.
Ngưỡng đỉnh vẫn lượng tử theo ADC và leakage_gain, tính một lần khi boot;
không thêm phép toán float vào đường ISR theo mẫu hoặc SysTick.
Timeout zero-cross 30 ms, nhịp LED/còi và cực tính GPIO vẫn là cấu hình firmware.

## Chọn chế độ dữ liệu RELEASE / DEBUG

Mở Core/Protection/Protection_Cfg.h và chỉ thay một dòng:

    #define PROTECTION_MODE PROTECTION_MODE_RELEASE

Đây là mặc định. Toàn bộ Protection đọc MeasureResult thật do Measurement cập nhật, gồm cả startup_complete, RMS, nhiệt độ, chẩn đoán và leakage_trip_flag trong DMA ISR. MeasureResult_test không được biên dịch vào firmware nên không chiếm RAM.

Khi cần mô phỏng trên debugger, đổi thành:

    #define PROTECTION_MODE PROTECTION_MODE_DEBUG

Build và nạp lại firmware. Thêm MeasureResult_test vào Expressions/Watch. Biến này là volatile để cả main và ISR đọc được thay đổi từ debugger khi bật tối ưu -Os. Ban đầu toàn bộ trường bằng 0 và startup_complete=0, relay giữ OFF. Gán dữ liệu bình thường trước, ví dụ:

    MeasureResult_test.V_in_rms = 220
    MeasureResult_test.V_out_rms = 220
    MeasureResult_test.I_in_rms = 1
    MeasureResult_test.I_leak_rms = 0.005
    MeasureResult_test.Temperature = 25
    MeasureResult_test.startup_complete = 1
    MeasureResult_test.fast_leakage_ready = 1

Các cờ temperature_fault, adc_clipped_mask và leakage_trip_flag mặc định là 0. Sau khi đã có dữ liệu hợp lệ, thay từng trường để thử OV/UV/OC/OT/LEAK, thời gian phục hồi hoặc latch. Đơn vị dòng là ampere, nhiệt độ là độ C, điện áp là V RMS. ProtectionInit không ghi đè dữ liệu test người dùng; reset MCU khởi tạo lại biến về 0.

Chế độ này độc lập cấu hình Debug/Release của CubeIDE và macro DEBUG do compiler tạo. CubeIDE Debug vẫn dùng dữ liệu thật nếu PROTECTION_MODE đang là RELEASE. Khi đổi nguồn phải rebuild/nạp lại; không có công tắc runtime. Giá trị mode không hợp lệ gây lỗi biên dịch.

DEBUG chỉ mô phỏng đầu vào Protection: ngưỡng và logic relay/LED/buzzer vẫn chạy, nhưng dữ liệu đo thật không kích các kiểm tra ngưỡng/cờ rò. ADC/DMA và callback zero crossing vẫn chạy thật. Lỗi DMA nghiêm trọng vẫn đi qua Error_Handler và EmergencyStop. Không có bộ giả lập zero crossing trong chế độ này.

Ngắt cả relay LINE và NEUTRAL khi một trong các kết quả đo vi phạm ngưỡng sau:

| Đại lượng | Trường trong nguồn đo đã chọn | Ngưỡng |
|---|---|---|
| Quá áp vào | `V_in_rms` | >=240 V RMS |
| Thấp áp vào | `V_in_rms` | <=190 V RMS |
| Điện áp ra | `V_out_rms` | 240 V RMS |
| Dòng tải | `I_in_rms` | 2,8 A RMS |
| Dòng rò | `I_leak_rms` | 0,030 A RMS |
| Nhiệt độ | `Temperature` | 70 °C |

Các số trong bảng là mặc định, có thể cấu hình như phần Ngưỡng lưu Flash. Quá áp Vin/Vout, thấp áp Vin và quá nhiệt được phép tự phục hồi theo điều kiện bên dưới. Quá dòng, dòng rò và lỗi phép đo giữ latch đến khi reset MCU. `Protection_GetFaultMask()` trả về các bit đang giữ; các bit phục hồi được xóa cùng lúc khi đóng lại relay. NTC lỗi, giá trị không hữu hạn, RMS âm hoặc ADC clipping cũng ngắt relay với bit `PROTECTION_FAULT_MEASUREMENT`.

Khi khởi động, cả hai relay ngắt, LED sáng liên tục và còi tắt. Sau bản đo hoàn chỉnh đầu tiên, nếu không lỗi, cả hai relay đóng; LED vẫn sáng và còi vẫn tắt. Khi lỗi xuất hiện, hệ thống giữ yêu cầu ngắt, đồng thời LED và còi bắt đầu bật/tắt cùng pha, không chặn chương trình: bật 250 ms, tắt 250 ms, chu kỳ đầy đủ 500 ms. Còi dùng chân `BUZZER_CTRL` (PA7), giả định là buzzer chủ động kích mức HIGH; `PROTECTION_BUZZER_ACTIVE_HIGH` cho phép đổi cực tính. Còi thụ động/loa cần mạch tạo âm hoặc PWM riêng. Hai relay nhận lệnh tắt tại sự kiện zero crossing Vin tiếp theo; nếu không có sự kiện thì tắt cưỡng bức sau tối đa 30 tick 1 ms. Lỗi lặp lại không khởi động lại thời gian chờ hoặc nhịp LED/còi. Báo lỗi tiếp tục trong khi chờ phục hồi; nếu có lỗi không cho phép phục hồi thì báo đến khi reset. `Protection_IsTripPending()` cho biết đang chờ zero crossing.

## Tích hợp và thời gian phản ứng

- `main.c`: gọi trực tiếp `ProtectionInit()`, `MeasurementInit()` rồi `Scheduler_Init()` sau khởi tạo GPIO/clock; vòng lặp gọi `Scheduler_MainFunction()`. Task 1 ms gọi trực tiếp `Measurement_MainFunction()` rồi `Protection_MainFunction()`.
- `stm32f0xx_it.c`: SysTick gọi `Scheduler_Tick1msISR()` → `Rte_Call_Protection_Tick1msISR()` → `Protection_Tick1msISR()`. Handler ngắt ADC1 DMA1 Channel 1 gọi trực tiếp `Measurement_DMA_IRQHandler()` rồi `Protection_FastLeakageISR()`; nguồn ngắt được chú thích tại hàm và nơi gọi.
- `Scheduler_Init()` bật ngắt SysTick của bộ đếm 1 ms đã được cấu hình bởi `LL_Init1msTick()`. Không cấu hình lại tần số timer. `ProtectionInit()` không tự bật ngắt SysTick.
- Measurement gọi macro `Rte_Call_Measurement_ZeroCross()` trong `Rte.h`, được mở rộng trực tiếp thành `Protection_ZeroCrossISR()` trong ngắt DMA. Đường dòng rò nhanh cũng chờ zero crossing hoặc timeout; nếu cờ dòng rò xuất hiện trước crossing trong cùng nhóm mẫu thì crossing đó được dùng để ngắt.
- Khi DMA báo lỗi nghiêm trọng, Measurement gọi trực tiếp `Error_Handler()` → `Rte_Call_Protection_EmergencyStop()`. Protection ngắt ngay trước khi tắt toàn bộ ngắt CPU. Vòng lặp lỗi đọc cờ COUNTFLAG của bộ đếm SysTick 1 ms và gọi macro `Rte_Call_Protection_Tick1msISR()` để LED/còi tiếp tục chu kỳ 500 ms dù ngắt CPU đã bị khóa. Cơ chế này phụ thuộc bộ đếm SysTick và CPU tiếp tục chạy; không tiếp tục scheduler hoặc phép đo trong vòng lặp lỗi.

Zero crossing dùng cả hai cạnh của Vin đã bù RC và loại offset DC, tách khỏi `ProcessFrequency()` vốn dùng cạnh lên của ADC thô. Có hysteresis và kiểm tra khoảng cách cạnh tương ứng 40..70 Hz, xác nhận hai khoảng nửa chu kỳ hợp lệ sau giai đoạn bias ban đầu. Ở 50 Hz, các cạnh cách nhau khoảng 10 ms. Bộ lọc, hysteresis và DMA nhóm 10 mẫu gây trễ phát hiện/thông báo; mô phỏng sin 50/60 Hz cho độ lệch thời điểm mẫu phát hiện không quá 300 µs, chưa gồm thời gian chờ DMA (xấp xỉ tối đa 1 ms khi xử lý kịp), thời gian ISR và phần cứng. Sự kiện biểu thị crossing vừa được phát hiện trong dữ liệu, không dự đoán thời điểm tương lai.

Đồng bộ này áp dụng cho **lệnh tắt cuộn relay**, không bảo đảm tiếp điểm mở đúng zero. Muốn đồng bộ tiếp điểm thực cần biết thời gian nhả, mạch dập xung cuộn dây, độ trễ hai relay và đo thực tế rồi dự đoán thời điểm phát lệnh sớm. Thời gian chờ 30 ms tính từ lúc phần mềm nhận biết lỗi, không tính từ lúc sự cố vật lý bắt đầu, và phụ thuộc SysTick/CPU tiếp tục chạy bình thường.

So sánh RMS/nhiệt độ không có trì hoãn xác nhận lỗi, nhưng dùng kết quả cửa sổ 2.000 mẫu ở 10 kHz của Measurement (200 ms). Thời điểm phát hiện phụ thuộc vị trí sự cố trong cửa sổ, phép lấy RMS/trung bình và thời gian xử lý vòng lặp. Đây không phải cam kết cắt điện trong 200 ms: sự cố gần ranh giới cửa sổ có thể cần cửa sổ tiếp theo mới vượt ngưỡng. Thời gian mở tiếp điểm relay cơ khí chưa được đo.

Đường ngắt nhanh dùng `leakage_trip_flag` có sẵn, được Measurement đặt trong DMA ISR theo **đỉnh/tức thời**, không phải RMS. Ngưỡng hiện tại `MEASUREMENT_LEAK_FAST_PEAK_A = 0,030 A`; lượng tử ADC hiện tại làm cờ tác động từ khoảng 33,6 mA tức thời. Vì vậy đường này có thể ngắt khi dòng rò RMS còn dưới 30 mA. DMA xử lý mỗi 10 mẫu (khoảng 1 ms), sau giai đoạn xác lập bias đầu tiên 200 ms. Measurement tính ngưỡng ADC lúc boot từ leak_fast_peak_a đã lưu và leakage_gain; Protection tiêu thụ cờ mà không xóa cờ này. Nguồn của cả đường nhanh và main được chọn đồng nhất bằng PROTECTION_MODE: RELEASE đọc MeasureResult, DEBUG đọc MeasureResult_test.

Giả định phần cứng: driver relay, LED và buzzer chủ động tác động mức HIGH, relay dùng tiếp điểm thường mở. Các macro `PROTECTION_RELAY_ACTIVE_HIGH`, `PROTECTION_LED_ACTIVE_HIGH` và `PROTECTION_BUZZER_ACTIVE_HIGH` trong header cho phép đổi cực tính. Mức GPIO mặc định trong `MX_GPIO_Init()` cũng phải phù hợp với board nếu đổi cực tính; mặc định hiện tại là LOW. Chưa xác minh cực tính, hiệu chuẩn hay thời gian phản ứng trên board thực.

## Kiểm chứng

Chạy `node Core/Protection/tests/run_protection_tests.mjs` với GCC native trong PATH; có thể truyền đường dẫn GCC làm đối số thứ nhất. Các mẫu `.c.in`/`.h.in` không được CubeIDE biên dịch vào firmware.

Kiểm thử gồm sát dưới/bằng/trên từng ngưỡng, nhiều lỗi đồng thời, giữ lỗi, nhịp LED, chờ zero crossing, timeout 30 ms không bị lỗi lặp lại kéo dài, dòng rò nhanh và ngắt khẩn cấp khi lỗi nghiêm trọng. Runner Protection cũng chạy mô phỏng kết hợp ADC/DMA–Measurement–Protection với bộ tạo tín hiệu analog độc lập. Bộ Measurement kiểm tra thêm crossing hai cạnh ở 50/60 Hz, bù pha RC, loại offset DC, mất AC, nhiễu và tràn bộ đếm mẫu. Chưa thử trên phần cứng.

Kết quả ngày 2026-10-21 sau cập nhật zero crossing: Protection đạt 2.709 kiểm tra, tích hợp đạt 15 kiểm tra, Measurement đạt 144 kiểm tra, không lỗi. Debug/Release build thành công, không cảnh báo. Flash text+data 10.584 byte / 16.384 byte; RAM data+bss 1.732 byte / 4.096 byte, đã gồm vùng heap/stack tối thiểu do linker dành trước.

Cập nhật ngày 2026-10-22 cho LED/còi: Protection đạt 7.721 kiểm tra, tích hợp đạt 1.019 kiểm tra, không lỗi. Bao gồm LED sáng/còi tắt khi khởi động và vận hành bình thường, bắt đầu báo ngay khi nhận lỗi, đồng pha qua nhiều chu kỳ, lỗi lặp lại không làm mất nhịp, giữ báo lỗi sau khi giá trị đo hồi phục, reset tắt còi và cập nhật báo lỗi khi IRQ đã khóa. Debug/Release build thành công, không cảnh báo; Flash text+data 10.828 byte, RAM data+bss 1.740 byte. Kiểm thử trên máy tính; chưa xác nhận âm thanh thực trên board.

## Tự phục hồi Vin/Vout và nhiệt độ ngày 2026-10-22

Biến toàn cục MeasureResult_test của người dùng được giữ trong chế độ DEBUG. Mọi kiểm tra ngưỡng, startup, dữ liệu lỗi và cờ dòng rò nhanh dùng cùng nguồn được chọn trong Protection_Cfg.h. RELEASE mặc định dùng MeasureResult thật. Không tự sao chép MeasureResult vào biến test. Measurement và callback zero crossing vẫn hoạt động theo đường tích hợp hiện tại.

Chỉ PROTECTION_FAULT_OV_IN, PROTECTION_FAULT_OV_OUT, PROTECTION_FAULT_UV_IN và PROTECTION_FAULT_OT được phép tự phục hồi. Nếu đã giữ bất kỳ bit nào khác (OC, LEAK, MEASUREMENT), relay không tự đóng dù các số đo đã bình thường. Kể cả tổ hợp lỗi hỗn hợp cũng giữ nguyên latch cho đến reset.

| Cấu hình trong Protection.h | Mặc định |
|---|---|
| PROTECTION_OV_RECOVER_V | 230 V, dùng cho cả Vin và Vout |
| PROTECTION_UV_IN_RECOVER_V | 200 V, chỉ dùng cho Vin |
| PROTECTION_OT_RECOVER_C | 65 °C |
| PROTECTION_RECOVER_HOLD_MS | 1000 tick SysTick 1 ms |

Ngưỡng cắt vẫn là >=240 V và >=70 °C. Phục hồi yêu cầu đồng thời 200 V <= Vin <=230 V, Vout <=230 V, Temperature <=65 °C, startup_complete !=0, số đo hợp lệ, không có lỗi hiện hành hoặc cờ dòng rò và không có bit lỗi bị cấm phục hồi. Thấp áp chỉ áp dụng cho Vin: cắt khi <=190 V; không kiểm tra thấp áp Vout vì đầu ra có thể về 0 V khi relay ngắt. Các giá trị 230 V/65 °C/1000 ms là mặc định phần mềm có thể chỉnh tại header, chưa được xác nhận trên board.

Trình tự: yêu cầu ngắt như cũ -> relay nhận lệnh OFF tại zero crossing hoặc timeout -> main nhận thấy đủ điều kiện và bật recovery_armed -> SysTick tăng recovery_elapsed_ms -> main kiểm tra lại toàn bộ điều kiện rồi xóa fault_mask và đóng hai relay. Sau phục hồi LED sáng liên tục, buzzer tắt. Chỉ main loop được đóng lại relay; ngắt SysTick không tự xóa lỗi. Lệnh đóng lại không đồng bộ zero crossing.

Trong vùng hysteresis (190..200 V ở Vin, 230..240 V hoặc 65..70 °C), lỗi cũ vẫn giữ và thời gian chờ bị xóa. Nếu bất kỳ điều kiện nào không còn đạt hoặc xuất hiện lỗi mới thì phải đếm lại đủ thời gian. RequestTrip hủy thời gian chờ ở cả đường main và DMA ISR; các cập nhật chốt lỗi/GPIO được bảo vệ khỏi tranh chấp ngắt. Timer bão hòa, không tràn khi main chậm. Điều kiện ổn định dựa trên các giá trị gần nhất mà main quan sát; không bổ sung cờ freshness cho MeasureResult_test. SysTick và main phải tiếp tục hoạt động để phục hồi.

Ví dụ test: với startup_complete=1, Vin=Vout=220, Iin=1 A, Ileak=0,005 A, Temperature=25 °C và các cờ lỗi=0, tăng Vin lên 300 V để tạo lỗi. Đưa Vin về 220 V, giữ Vin trong 200..230 V, Vout <=230 V và Temperature <=65 °C; sau khi relay đã ngắt và đếm đủ 1000 tick, relay đóng lại. Nếu thay bằng lỗi Ileak=0,04 A thì đưa về 0,005 A cũng không phục hồi.

Kiểm chứng sau thay đổi: Protection 7863 kiểm tra; tích hợp 1025 kiểm tra, 0 lỗi. Bao gồm đúng biên ngưỡng phục hồi, hysteresis, thời gian chờ, lỗi lặp lại, lỗi hỗn hợp, ngắt relay xong trước khi đếm, mất startup-valid, timer không tràn và tranh chấp DMA tại lúc phục hồi. Fixture tích hợp gán dữ liệu test tường minh, không thay nguồn dữ liệu trong firmware. Debug/Release build thành công không warning/error. Flash 11040 byte (67,38%), RAM theo size 1812 byte (44,24%), gồm vùng heap/stack linker hiện tại; tăng 212 byte Flash và 8 byte RAM so với bản trước thay đổi này. Chưa thử trên board.

## Bảo vệ thấp áp Vin

PROTECTION_UV_IN_V = 190 V: khi startup_complete !=0 và Vin RMS hợp lệ <=190 V, đặt PROTECTION_FAULT_UV_IN = 1U << 6 (0x40, thập phân 64). Các bit cũ giữ nguyên giá trị. Vin âm/không hữu hạn vẫn thuộc lỗi phép đo không được tự phục hồi. Vin=0 sau startup được coi là mất áp và kích bảo vệ; trước startup không kiểm tra UV. Vout không có bảo vệ thấp áp.

UV dùng cùng đường ngắt relay tại zero crossing hoặc timeout 30 tick khi mất crossing, cùng báo LED/buzzer. UV được tự phục hồi khi Vin >=200 V và thỏa toàn bộ điều kiện phục hồi còn lại trong 1000 tick sau khi relay đã ngắt. Các ngưỡng 190/200 V nằm trong Protection.h. Giữ MeasureResult_test khi chọn chế độ DEBUG; không thay thuật toán Measurement.

Kiểm chứng: Protection 7904 kiểm tra, tích hợp 1030 kiểm tra, 0 lỗi; có sát trên/bằng/dưới 190 V, biên phục hồi 200 V, hysteresis, mất áp 0 V và timeout, startup, Vout=0 vẫn cho phép đóng lại, lỗi hỗn hợp không phục hồi và đường zero crossing/scheduler thật. Debug/Release build thành công, không cảnh báo. Flash 11100 byte (67,75%), RAM theo size 1812 byte (44,24%); tăng 60 byte Flash và không tăng RAM so với bản trước UV. Chưa kiểm chứng trên board.

## Kiểm chứng macro chọn nguồn

Bộ kiểm thử Protection chạy toàn bộ tình huống ở cả hai chế độ: RELEASE mặc định đạt 7906 kiểm tra, DEBUG đạt 7904 kiểm tra; tích hợp đạt 1030 kiểm tra ở mỗi chế độ, 0 lỗi. Có kiểm tra RELEASE phản ứng với cờ dòng rò thật, DEBUG bỏ qua dữ liệu thật khi dữ liệu test bình thường, và từ chối mode không hợp lệ.

Cả hai cấu hình CubeIDE Debug/Release build thành công với mode mặc định RELEASE, không warning/error: Flash 11100 byte, RAM theo size 1740 byte (42,48%). Symbol MeasureResult_test không tồn tại trong hai ELF này; tổng RAM giảm 72 byte do bỏ biến test 68 byte và thay đổi căn chỉnh section. Một bản ELF Cortex-M0 với mode DEBUG cũng đã compile/link riêng ở thư mục tạm: Flash 11088 byte, RAM 1812 byte; symbol MeasureResult_test tồn tại đúng 68 byte. File cấu hình và sản phẩm build chính cuối cùng giữ mode RELEASE. Chưa thử trên board.
