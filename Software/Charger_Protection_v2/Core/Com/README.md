# com — UART và giao thức v1

`Core/Src/usart.c` cấu hình USART1 PA9 TX / PA10 RX 115200 8N1. `com.c` quản lý RX ring 128 byte, TX frame 80 byte và xử lý lệnh. Không tạo thêm module UART. Không dùng malloc/printf trên MCU.

Luồng chạy:

```text
PC ↔ USART1 IRQ ↔ com buffer/parser (task 10 ms)
                           ↕ RTE
              Calib / snapshot Measurement / Protection / reset
```

IRQ USART ưu tiên 0, DMA ADC ưu tiên 1, SysTick ưu tiên 3. UART không có FIFO RX; một byte ở 115200 8N1 mất khoảng 86.8 µs. Cho IRQ chuyển byte ngắn ngắt được xử lý ADC theo lô để tránh chờ cả lô ADC mới đọc RDR. IRQ chỉ chuyển byte và ghi nhận lỗi; không chờ TX hoặc tính float. Lệnh được xử lý ở main loop qua `Scheduler_Task10ms`. Mỗi lần xử lý tối đa một lệnh; chờ TX hoàn tất trước khi lấy lệnh tiếp theo. Các lời gọi sang Calib, Rte snapshot, Protection và application reset đều đi qua RTE. Thời gian parser lấy từ bộ đếm SysTick của Scheduler; không thêm xử lý giao thức vào ISR 1 ms. Vẫn cần đo thời gian ISR và kiểm tra UART cùng ADC trên board.

## Frame

```text
A5 5A | VERSION:u8 | COMMAND:u8 | SEQ:u8 | LENGTH:u8 | PAYLOAD | CRC16:u16_LE
```

`VERSION=1`. `LENGTH` chỉ tính payload. CRC-16/CCITT-FALSE: init `FFFF`, poly `1021`, không phản chiếu, xorout 0, tính từ VERSION đến hết PAYLOAD. Vector `123456789` → `29B1`. Header SOF và CRC không tham gia phép tính CRC. Tất cả số nguyên và float32 IEEE-754 đều little endian.

Request tối đa 40 byte payload. Response tối đa 72 byte payload, COMMAND = request COMMAND OR `80`, SEQ giữ nguyên. Byte đầu payload response luôn là **status**. Các bảng dưới mô tả phần dữ liệu **sau status**. PC gửi từng lệnh và đợi phản hồi tương ứng (command + sequence), timeout gợi ý 3 s; không pipeline. SEQ được PC tăng modulo 256, dùng đối chiếu phản hồi, không có cache chống gửi lặp.

Parser bỏ khung CRC sai, length > 40, hoặc command có bit response; tăng RX lỗi khi CRC sai/lỗi UART/tràn ring. Khung dở bị bỏ khi lần gọi main tiếp theo cách lần tiêu thụ byte trước ít nhất 200 ms (đồng hồ này dừng trong thời gian IRQ bị khóa để ghi Flash). Lỗi UART hoặc mất byte làm bỏ toàn bộ RX đang chờ để không ghép nhầm khung. Sau timeout PC gửi lại một lệnh **đọc** để kiểm tra; không tự lặp lệnh ghi/reset.

## Commands

Firmware 2.7 thêm lệnh đọc chuỗi vượt ngưỡng fast dài nhất cho tool 1.7,
tab Debug. Xác nhận fast vẫn dùng 20 mẫu liên tiếp sau lọc.

| Cmd | Lệnh | Payload request | Dữ liệu response thành công (sau status) |
|---|---|---|---|
| 01 | GET_INFO | rỗng | `<BBBBIHH>`: protocol=1, fw major=2, fw minor=7, flags, sequence Flash u32, capabilities u16, RX error count u16 |
| 02 | GET_MEAS | rỗng | `<15fIIBBB>` — 71 byte, mô tả dưới |
| 03 | GET_CALIB | rỗng | `<5f>`: hệ số đang được dùng để đo |
| 04 | GET_STORED | rỗng | `<5f>`: hệ số trong Flash; nếu chưa có: status `84`, không có dữ liệu |
| 05 | SAVE_CALIB | `<5f>` | flags u8 (cả status Calib lỗi cũng trả flags) |
| 06 | RESTORE_DEFAULTS | rỗng | flags u8 (cả status Calib lỗi cũng trả flags) |
| 07 | RESET | rỗng | rỗng; chỉ reset khi ACK đã ra khỏi UART shift register (TC) |
| 08 | GET_PROTECTION | rỗng | `<9fI>`: ngưỡng đang áp dụng |
| 09 | GET_STORED_PROTECTION | rỗng | `<9fI>`: ngưỡng trong Flash; status `84` nếu trắng/lỗi hoặc chỉ có record Calib v1 |
| 0A | SAVE_PROTECTION | `<9fI>` | flags u8, giữ nguyên Calib đã lưu |
| 0B | RESTORE_PROTECTION_DEFAULTS | rỗng | flags u8, giữ nguyên Calib đã lưu |
| 0C | GET_LEAK_MAX | rỗng | `<ffB>`: Ileak peak MAX [A], Ileak RMS MAX [A], fast offset ready u8 |
| 0D | GET_LEAK_COUNT_MAX | rỗng | `<B>`: leakage_over_count_max [mẫu], uint8 |

`<9fI>`: ov_v, uv_in_v, oc_a, leak_a, ot_c, ov_recover_v,
uv_in_recover_v, ot_recover_c, leak_fast_peak_a (float32), recover_hold_ms
(uint32). Điện áp V RMS; dòng A; nhiệt độ °C; thời gian ms. Xem giới hạn và
mặc định tại [Protection](../Protection/README.md). SAVE_CALIB/RESTORE_DEFAULTS
giữ nguyên ngưỡng đã lưu; SAVE_PROTECTION/RESTORE_PROTECTION_DEFAULTS giữ
nguyên Calib đã lưu, kể cả nhóm kia đang chờ reset. Cờ reset_required dùng chung.
Các payload cũ và số command 01..0C không đổi; capabilities mới là `1FFF`.
GET_LEAK_COUNT_MAX có một byte dữ liệu sau status (payload tổng hai byte),
giữ chuỗi dài nhất đến Reset MCU, 0..20 với N hiện tại. Sau khi chốt fast
giữ 20. Tool chỉ gửi 0D khi INFO có bit capability `1000`; firmware cũ
hiện "Chưa hỗ trợ". Biến nguồn là `MeasureResult.leakage_over_count_max`.
GET_LEAK_MAX có 9 byte dữ liệu sau status, payload tổng 10 byte. Peak là
độ lệch tuyệt đối Ileak sau LPF so với offset fast, tích lũy từ khi offset
sẵn sàng; RMS MAX gồm cửa sổ đầu. Hai MAX giữ trong RAM tới Reset MCU,
tiếp tục cập nhật sau khi đã chốt lỗi, không có lệnh xóa riêng hoặc ghi
Flash. Firmware 2.2 công bố bit capability `0800`; client cũ vẫn dùng MEAS
72 byte, client mới chỉ gửi 0C nếu có bit này. Chưa sẵn sàng thì peak=0,
ready=0; không nên diễn giải là đã đo được đỉnh bằng 0.

`<5f>` thứ tự: vin_gain, vout_gain, current_gain, leakage_gain, temperature_offset_c. Gain 0.25…4; offset −20…20 °C; không nhận NaN/Inf. Mặc định `{1,1,1,1,0}` chỉ tắt bù sai số, giữ nguyên hệ số chuyển đổi phần cứng.

flags: bit0 đang dùng bản đọc từ Flash ở boot; bit1 Flash hiện có record hợp lệ; bit2 cần reset vì đã bắt đầu ghi Flash. Bit0 và bit1 có thể khác nhau sau ghi lỗi. sequence là sequence của record hợp lệ trong Flash, record đầu có sequence=0; chưa có record hợp lệ cũng trả 0, phân biệt bằng flags. capabilities `1FFF` hỗ trợ 13 lệnh (bit0 ứng với cmd01); RX error count u16 vòng về 0 khi tràn. Với record Calib v1, bit0 có thể bật nhưng ngưỡng vẫn mặc định; GET_STORED_PROTECTION phân biệt trường hợp này.

GET_MEAS: 15 float theo thứ tự:

1. Vin RMS [V]
2. Iin RMS [A]
3. P active [W]
4. S apparent [VA]
5. Power factor [không đơn vị]
6. Frequency [Hz]
7. Vout RMS [V]
8. Ileak RMS **[A]**
9. Temperature [°C], có thể NaN khi lỗi NTC
10. Vin ADC DC offset [V tại ADC]
11. Vout ADC DC offset [V tại ADC]
12. Iin ADC DC offset [V tại ADC]
13. Ileak ADC DC offset [V tại ADC]
14. Leakage ADC RMS [count]
15. Temperature ADC average [count]

Tiếp theo: clipped_mask u32 (Vin bit0, Vout bit1, Iin bit2, Ileak bit3, nhiệt độ bit4), fault_mask u32 (theo Protection.h), startup_complete u8, leakage_trip_flag u8, temperature_fault u8. Snapshot được lấy tại main loop sau task đo; cờ fast leakage có thể thay đổi độc lập ở DMA ISR. `startup_complete` báo đã có cửa sổ đo đầu tiên, không phải timestamp/freshness. Trong service state sau ghi Flash, số đo là cửa sổ cuối, phải kiểm tra INFO flags bit2 trước khi dùng để tính Calib.

## Status

| Hex | Tên | Ý nghĩa |
|---|---|---|
| 00 | OK | thành công |
| 01 | UNCHANGED | cùng dữ liệu trong Flash; không ghi lại |
| 02 | INVALID | tham số Calib không hợp lệ |
| 03 | BUSY | chưa thể ghi, không vào service state |
| 04 | FLASH_ERROR | lỗi ghi/verify; đã vào service state, cần reset |
| 80 | BAD_COMMAND | command chưa hỗ trợ |
| 81 | BAD_LENGTH | sai độ dài cho command |
| 82 | BAD_CRC | dành trước; hiện khung CRC sai bị bỏ, không có ACK |
| 83 | BAD_VERSION | version chưa hỗ trợ |
| 84 | NO_STORED | không có record Flash hợp lệ |

SAVE hợp lệ và có thay đổi mở relay, ngừng ADC/DMA trước khi erase page Calib duy nhất tại `0x08003C00`, ghi CRC32 và commit cuối. Không tự áp dụng hệ số và không tự reset. GET_CALIB vẫn trả hệ số cũ; GET_STORED trả bản mới. PC đọc lại bản lưu, nhận ACK rồi chủ động gửi RESET. Ghi lỗi cũng dừng đo để tránh chạy tiếp trên trạng thái acquisition chưa đầy đủ. INVALID/UNCHANGED/BUSY không tự vào service state, nhưng trạng thái chờ reset trước đó vẫn được giữ.

Chỉ dùng một page 1 KB: mất nguồn khi thay bản ghi có thể xóa mất bản trước; boot dùng mặc định nếu CRC/version/commit/giới hạn không hợp lệ. Không có lệnh điều khiển đóng relay. Ngưỡng chỉ áp dụng sau reset, không thay trực tiếp trong ISR hay khi đang chạy.
