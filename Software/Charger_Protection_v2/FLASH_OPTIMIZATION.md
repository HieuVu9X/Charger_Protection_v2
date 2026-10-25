# Tối ưu Flash

## MAX số mẫu trên Debug — firmware 2.7, tool 1.7

Thêm `MeasureResult.leakage_over_count_max`, được main sao chép từ MAX
của ISR trước khi đợi cửa sổ RMS. UART command 0D trả một byte số mẫu,
INFO capability `1FFF`; payload MEAS và GET_LEAK_MAX giữ nguyên.
Tool đọc khi có capability `1000`, hiển thị trong Debug, đơn vị mẫu;
firmware cũ hiện chưa hỗ trợ. MAX giữ đến Reset MCU, N vẫn bằng 20.

Debug/Release: text 13.716 + data 80 = **13.796 byte Flash (89,82%)**,
còn 1.564 byte; tăng 44 byte. RAM data 80 + bss 2.184 = **2.264 byte**,
giữ nguyên tổng theo linker dù MeasureResult tăng từ 76 lên 80 byte.
Measurement 709, Calib 1.638, Protection 8.007/8.005 và tích hợp
1.037/1.030 kiểm tra qua. UART 19 bài, GUI 7 bài qua; kiểm tra 9 dòng
Debug đều hiển thị và các nút lưu vẫn trong Calib. Build hai cấu hình qua,
còn cảnh báo target báo cáo `.su`/`.cyclo` cũ; chưa thử trên board.
Đã xuất HEX/BIN, không có symbol chưa liên kết. SHA-256 hai binary giống nhau:
`38C1135C02D249340C9C9182EFD44367B1EA04AB6FFEAC8DC13F966C78A960C3`.

## Quan sát bộ đếm N=20 — firmware 2.6

Đã kiểm tra assembly bản 2.5: counter vẫn tăng/ghi RAM ở mỗi mẫu đạt
ngưỡng. Đánh dấu counter volatile để bảo đảm quan sát qua debugger và thêm
`leakage_over_count_max`, giữ chuỗi dài nhất trước khi chốt tới Reset MCU.
Không đổi N=20, ngưỡng, cutoff, RMS, MAX dòng rò hoặc giao thức UART.

Flash text 13.672 + data 80 = **13.752 byte (89,53%)**, còn 1.608 byte.
RAM data 80 + bss 2.184 = **2.264 byte (55,27%)**, còn 1.832 byte, đã gồm
vùng heap/stack linker. Tăng 32 byte Flash, 8 byte RAM theo linker.
Hai counter uint8_t đều có symbol để xem bằng Live Expressions. Dùng ELF
mới đi cùng HEX mới vì vị trí các symbol khác có thể đổi sau build.

707 kiểm tra Measurement qua, gồm giữ max=2/3 sau các xung đã về count=0
trong cùng buffer, tăng lần lượt 1..20, giữ qua reset chuỗi và xóa ở boot.
Calib 1.638, Protection 8.007/8.005, tích hợp 1.037/1.030 qua. Debug/Release
build không lỗi, chỉ cảnh báo báo cáo `.su`/`.cyclo` cũ; chưa thử trên board.
Đã xuất HEX/BIN; hai binary cùng SHA-256:
`470FCE71077205604A6E9D5A6521B33ACD1F2A5A0D5D6296A21802D7363C77AE`.

## Fast leakage xác nhận 20 mẫu — firmware 2.5

N tăng từ 10 lên 20; cutoff 1 kHz, ngưỡng/RMS và UART giữ nguyên. Flash
**13.720 byte**, còn 1.640 byte; RAM **2.256 byte**, còn 1.840 byte, bằng
bản N=10. Từ mẫu vượt đầu đến mẫu xác nhận thứ 20 là 1,9 ms, chưa gồm LPF,
DMA hoặc relay. FastLeakTrip chụp mẫu xác nhận; MAX vẫn giữ đỉnh chưa đủ N.

Kiểm chứng: Measurement 581; Calib 1.638; Protection 8.007 RELEASE /
8.005 DEBUG; tích hợp 1.037/1.030 đều qua. Debug/Release build không lỗi,
còn cảnh báo `.su`/`.cyclo` cũ. HEX/BIN đã xuất; chưa thử trên board. Tool
1.4 tiếp tục dùng được. Hai binary cùng SHA-256:
`3F52D6E168A55BC8F3DC74ECCB5F91BD4A82DF10040200A8799EED22503FD67E`.

## Fast leakage xác nhận 10 mẫu — firmware 2.4

Tăng N từ 5 lên 10, cutoff/ngưỡng/RMS/giao thức giữ nguyên. Tổng Flash
**13.720 byte**, còn 1.640 byte; RAM **2.256 byte**, còn 1.840 byte, bằng
bản N=5. Đã build Debug/Release không lỗi và xuất HEX/BIN. Còn cảnh báo
target báo cáo `.su`/`.cyclo` cũ. Measurement 461 kiểm tra, Protection
8.007 RELEASE/8.005 DEBUG, tích hợp 1.037/1.030 đều qua. Tool 1.4 dùng
tiếp; các bộ Calib/UART/GUI không đổi so với mốc trước. Chưa thử trên board.

FastLeakTrip chụp mẫu xác nhận thứ 10; MAX vẫn ghi các đỉnh ngắn. Binary
Debug/Release cùng SHA-256:
`10A8D2EBF038D19CD0619C1D486FB2EBE559D9CA844ECD872D0D59093BCA3281`.

## Fast leakage xác nhận 5 mẫu — firmware 2.3

Debug/Release: text 13.640 + data 80 = **13.720 byte Flash (89,32%)**,
còn 1.640 byte. RAM data 80 + bss 2.176 = **2.256 byte (55,08%)**, còn
1.840 byte, đã gồm vùng heap/stack linker. Tăng 60 byte Flash so với mốc
MAX bên dưới; bộ đếm uint8_t tận dụng căn chỉnh nên tổng RAM không đổi.
Cutoff 1 kHz, ngưỡng, RMS, Calib, UART/capabilities và tool 1.4 giữ nguyên.

Chốt fast sau 5 mẫu liên tiếp sau lọc đạt ngưỡng. Một mẫu dưới ngưỡng hủy
chuỗi; bộ đếm giữ qua DMA/window. Offset giữ cố định trong chuỗi candidate.
MAX vẫn giữ đỉnh chưa được xác nhận; FastLeakTrip chỉ ghi ở mẫu xác nhận.

Kiểm chứng: Measurement 401; Calib 1.638; Protection 8.007 RELEASE /
8.005 DEBUG; tích hợp 1.037/1.030; UART 17 bài; audit sáu ca qua. Hai cấu
hình ARM build không lỗi; còn cảnh báo báo cáo `.su`/`.cyclo` cũ. Đã xuất
HEX/BIN, chưa chạy trên board. SHA-256 binary giống nhau giữa hai cấu hình:
`D1F753218D5975447434002A74009C99D5E8EE68BC7C78F9CC150870B4F5E073`.

## Ileak MAX trong MCU và tool PC 1.4 (2026-10-25)

Debug/Release: text 13.580 + data 80 = **13.660 byte Flash (88,93%)**,
còn 1.700 byte. RAM data 80 + bss 2.176 = **2.256 byte (55,08%)**, còn
1.840 byte; đã gồm vùng heap/stack linker. Tăng 52 byte Flash và 16 byte
RAM so với bản first-trip ngay dưới. Không đổi linker hoặc trang CALIB.

ISR giữ đỉnh tuyệt đối sau LPF bằng Q19, gồm cả các đỉnh sau lỗi. Main đổi
sang A khi có kỷ lục mới và giữ RMS MAX từ cửa sổ đầu. Reset MCU xóa cả
hai. Hai float append cuối MeasureResult_t (76 byte), trường cũ giữ vị trí.
GET_MEAS vẫn 72 byte payload; GET_LEAK_MAX (0C) trả `<ffB>` theo capability
`0800`, firmware 2.2/caps `0FFF`. Không tăng bộ đệm UART hay ghi Flash.
Tool 1.4 hiển thị mA, polling bằng worker cũ và hỗ trợ firmware cũ.

Kiểm chứng: Measurement 329; Calib 1.638; Protection 8.007 RELEASE /
8.005 DEBUG; tích hợp 1.033/1.030; giao thức UART 17 bài; GUI 7 bài.
Build ARM và đóng gói/smoke-test EXE qua. Cảnh báo `.su`/`.cyclo` cũ còn;
chưa kiểm chứng trên board. Binary Debug/Release cùng SHA-256:
`6C7A66A51FFBD93A5574895066164DFD1C474EEBD76C29538CE01FC630138B54`.

## Bản chụp fast trip đầu tiên (2026-10-25)

Debug/Release hiện dùng text 13.528 + data 80 = **13.608 byte Flash
(88,59%)**, còn 1.752 byte. RAM data 80 + bss 2.160 = **2.240 byte
(54,69%)**, còn 1.856 byte; đã gồm vùng heap/stack linker. Snapshot volatile
`FastLeakTrip` dài 24 byte; do bố trí/căn chỉnh section, tổng RAM tăng 16
byte, Flash tăng 92 byte so với mốc LPF ngay dưới. Không đổi linker, Calib,
ngưỡng hoặc bộ lọc. `MeasureResult_t` vẫn 68 byte, payload UART không đổi.

Ghi ADC thô, sau lọc, offset, độ lệch, ngưỡng count và chỉ số mẫu đúng lần
fast trip đầu tiên. Chỉ đổi ba giá trị sang float một lần khi đã chốt cờ;
không thêm float trên mẫu khỏe. Xem README Measurement để đọc Watch.
Kiểm tra qua: Measurement 317, Calib 1.638, Protection 8.007/8.005,
tích hợp 1.033/1.030 RELEASE/DEBUG, UART 15 bài. Hai cấu hình build không
lỗi, chỉ còn cảnh báo `.su`/`.cyclo` cũ. Chưa thử trên board.

Binary Debug/Release có cùng SHA-256:
`1EC9A971018B0F8FDB662F42894D526310D090A209F3E7A1925CD58C780B8B8B`.
Đã xuất `.hex`/`.bin`. Các mục dưới là số liệu trước snapshot chẩn đoán.

## Low-pass ADC cho năm kênh (2026-10-25)

Debug/Release: text 13.436 + data 80 = **13.516 byte Flash (87,99%)**,
còn 1.844 byte trong vùng firmware 15 KB. RAM data 80 + bss 2.144 =
**2.224 byte (54,30%)**, còn 1.872 byte, đã gồm vùng heap/stack linker.
So với trước LPF: tăng 316 byte Flash và 64 byte RAM. CALIB vẫn 1 KB riêng.
Mảng debug global của người dùng giữ nguyên.

LPF dùng chung mã IIR fixed-point, trạng thái ADC Q16, đầu ra Q8; không
thêm float theo mẫu hay bộ đệm mẫu. Trường tổng NTC Q8 dùng phần padding
cũ của Window_t (struct vẫn 104 byte). `MeasureResult_t` vẫn 68 byte,
UART và bản ghi Calib/Protection không đổi. Đáp ứng Vin/Vout/Iin có thêm
một pole đồng bộ; Ileak RMS/fast và NTC dùng bộ lọc riêng. Xem README
Measurement để biết ảnh hưởng biên độ, thời gian và tham số tần số cắt.

Kiểm tra qua: Measurement 308; Calib 1.638; Protection 8.007 RELEASE /
8.005 DEBUG; tích hợp 1.033 RELEASE / 1.030 DEBUG; UART 15 bài. Audit sáu
ca đã chạy lại, có ghi số RMS/fast sau lọc. Build cả hai cấu hình không lỗi;
chỉ còn cảnh báo target báo cáo `.su`/`.cyclo` cũ. Đã xuất `.hex`/`.bin`.
Chưa đo ISR hoặc đáp ứng bảo vệ trên board.

SHA-256 binary giống nhau giữa Debug/Release:
`EC181A0DA82E04075FB69C44EDA75A1211B317CBDF3A0FB4C2DF882D6C5219F5`.
Các mục sau là mốc trước cập nhật LPF.

## Offset riêng cho fast leakage (2026-10-25)

Số liệu hiện tại cho cả Debug và Release:

| Tài nguyên | Dùng | Còn trống |
| --- | ---: | ---: |
| Flash firmware 15.360 byte | 13.200 byte (85,94%) | 2.160 byte |
| RAM 4.096 byte | 2.160 byte (52,73%) | 1.936 byte |

Flash gồm text 13.120 + data 80; RAM gồm data 80 + bss 2.080, đã gồm vùng
heap/stack linker. CALIB vẫn là trang 1 KB riêng. Không đổi linker hoặc
cấu hình tối ưu. Binary 13.200 byte, SHA-256 giống nhau giữa hai mode:
`DC8585AE7697676E343C5CE691EDF42849D0AB70409705B25D9D5B191F27927E`.

So với source/build Debug có sẵn ngay trước đợt sửa này (Flash 12.976,
RAM 2.152), phần offset mới tăng 224 byte Flash và 8 byte RAM. Ba mảng
global debug `offset_v`, `ac_mean_v`, `rms_adc_v` đã có sẵn được giữ nguyên;
chúng chiếm 48 byte RAM so với mốc 12.952/2.104 ở phần lịch sử bên dưới.

Offset fast dùng Q19 với phép chia số nguyên; không thêm float theo mẫu,
không thêm bộ đệm. Cờ `fast_leakage_ready` dùng byte padding cũ của
`MeasureResult_t`: struct vẫn 68 byte, payload UART vẫn 72 byte. Các ngưỡng
Calib/Protection, bản ghi Flash và công thức RMS dùng offset volt thực giữ
nguyên. Relay chỉ đóng/phục hồi khi offset fast đã sẵn sàng.

Kiểm tra qua: Measurement 273; Calib 1.638; Protection 8.007 RELEASE /
8.005 DEBUG; tích hợp 1.033 RELEASE / 1.030 DEBUG; Scheduler 8.256; RTE 6;
UART 15 bài và audit sáu ca nhiễu/dịch bias. Debug/Release build không lỗi;
vẫn có 50 cảnh báo target báo cáo `.su`/`.cyclo` như trước. Đã xuất `.hex`
và `.bin`; chưa nạp board hoặc đo thời gian ISR thực tế.

## Tối ưu phép trừ soft-float (2026-10-25)

Số liệu tại thời điểm tối ưu phép trừ, sau bản RMS dùng offset volt thực:

| Tài nguyên | Trước | Sau |
| --- | ---: | ---: |
| Flash text + data | 14.160 byte | 12.952 byte |
| Tỷ lệ vùng firmware 15.360 byte | 92,19% | 84,32% |
| Flash còn trống cho firmware | 1.200 byte | 2.408 byte |
| RAM data + bss | 2.104 byte | 2.104 byte |

Tiết kiệm **1.208 byte (8,53%)**. Debug và Release có cùng kích thước và cùng
SHA-256 binary: `8057E6231CDF1D61E9058AA98125496AB1DE561EA682EE8F277E750FE0B3CF6E`.

Thư viện Cortex-M0 của GCC 13.3 dùng riêng 1.224 byte cho `__aeabi_fsub`,
trong khi `__aeabi_fadd` đã được liên kết. Trong `Measurement.c`, cung cấp
entry soft-float `__aeabi_fsub` bằng cách đảo bit dấu toán hạng phải rồi gọi
`__aeabi_fadd`. Entry mới dài 14 byte trong ELF; chênh lệch tổng còn phụ thuộc
căn chỉnh section. Symbol được giữ qua LTO để các phép trừ do compiler sinh
ở mọi module dùng entry này. Chỉ áp dụng cho ARM không có FPU.

Không đổi công thức đo, thứ tự phép toán, offset volt thực của RMS, đường
fast leakage, ngưỡng bảo vệ hay định dạng lưu Calib/Protection. Giữ nguyên
`-Os`, LTO, không bật fast-math. Trang CALIB 1 KB, heap và stack không đổi.

Kiểm chứng: Measurement 234 kiểm tra; trong đó so sánh 200.000 cặp phép trừ
với phép trừ float native, kiểm tra bit cho kết quả không phải NaN và kiểm tra
phân loại NaN, gồm số dưới chuẩn, signed zero và vô cực. Calib 1.638;
Protection 7.998 RELEASE / 7.996 DEBUG; tích hợp 1.030 mỗi mode; UART 15 bài
đều qua. Audit dòng rò khi chỉ cấp nguồn cho cùng kết quả trước tối ưu.
Đã kiểm tra disassembly: entry mới đảo bit dấu rồi gọi hàm cộng hiện có;
ELF không còn symbol chưa liên kết. Đã xuất `.hex`/`.bin` cả hai cấu hình.
Build không lỗi; vẫn có 50 cảnh báo target báo cáo `.su`/`.cyclo` như trước.
Chưa chạy kiểm chứng trên board. Các mục dưới đây là số liệu lịch sử.

## RMS dùng offset volt thực (2026-10-25)

Debug/Release: text 14.080 + data 80 = 14.160 byte Flash / 15.360 byte,
còn 1.200 byte. RAM data 80 + bss 2.024 = 2.104 byte, giữ nguyên. RMS dùng
offset ADC theo volt từ cùng cửa sổ; raw/filter/moment không còn trừ 2048.
DMA vẫn dùng số nguyên, không thêm bộ đệm toàn cửa sổ hoặc float theo mẫu.
Fast leakage giữ nguyên. Measurement 233 kiểm tra qua; Protection 7.998/
7.996 và tích hợp 1.030 mỗi mode đều qua. Build còn các cảnh báo .su/.cyclo
như trước. Số liệu các phần dưới là các mốc trước cập nhật này.

## Cập nhật cấu hình Protection (2026-10-24)

Debug và Release sau khi thêm ngưỡng lưu Flash: text 13.828 + data 80 =
13.908 byte Flash / 15.360 byte; còn 1.452 byte. RAM data 80 + bss 2.024 =
2.104 byte / 4.096 byte, đã gồm vùng heap/stack linker. Một record v2 80 byte
lưu cả Calib và Protection trong trang CALIB 1 KB cũ. Record v1 vẫn đọc được.
Không thêm trang Flash, không thay linker hay giảm vùng heap/stack.

Kiểm thử hiện tại: lưu Flash 1.638; Measurement 161; Protection 7.998 RELEASE
và 7.996 DEBUG; tích hợp 1.030 mỗi mode; Scheduler 8.256; RTE 6; Python
15 bài giao thức và 4 bài giao diện. Tất cả qua. Headless build cả hai cấu
hình không lỗi; vẫn có 50 cảnh báo target báo cáo .su/.cyclo đã mô tả bên dưới.

Các số liệu ở các phần bên dưới ghi lại đợt tối ưu trước cập nhật này.

Áp dụng cho cả Debug và Release với GCC 13.3 trong STM32CubeIDE 1.18.1.

| Tài nguyên | Trước tối ưu | Sau tối ưu + com |
| --- | ---: | ---: |
| Flash code + dữ liệu khởi tạo | 12.688 byte | 12.916 byte |
| Vùng firmware | 14 KB | 15 KB |
| Tỷ lệ trên vùng firmware | 88,50% | 84,09% |
| Flash còn cho code | 1.648 byte | 2.444 byte |
| RAM theo `size` (đã gồm vùng heap/stack linker) | 1.764 byte | 2.040 byte |

LTO và Calib một trang đưa firmware xuống 11.428 byte trước khi thêm giao
thức. Module com và các API hỗ trợ thêm 1.488 byte Flash, 256 byte RAM.
Hiện firmware dùng 12.916 byte Flash; RAM gồm data 40 + bss 2.000 byte,
trong bss đã có heap 0x150 và stack 0x300. CALIB hiện chỉ chiếm trang cuối
`0x08003C00..0x08003FFF` (1 KB). CRC/commit và định dạng record vẫn giữ nguyên;
một trang không giữ bản dự phòng khi đang xóa/ghi. Nếu record hỏng hoặc ghi
dở thì boot dùng mặc định. Vùng CALIB hiển thị 0 byte trong Build Analyzer
vì tham số được ghi lúc chạy, không chứa trong ELF.

## Cấu hình

- Giữ `-Os`, bật `-flto` khi compile và link để tối ưu giữa các module.
- `syscalls.c` và `sysmem.c` dùng `-fno-lto`. Nếu đưa cả hai vào LTO, các
  tham chiếu Newlib ở bước liên kết kéo thêm I/O và RAM không cần thiết.
  File vẫn được build và dùng bình thường nếu sau này có lời gọi cần tới.
- Linker giữ `EXTERN(Calib_Save)` cùng KEEP; com hiện cũng gọi API SAVE qua
  RTE. Dung lượng build đã tính cả code ghi Flash thực tế và giao thức UART.
- Các tùy chọn và ngoại lệ từng file được lưu trong `.cproject`, đã kiểm tra
  bằng CubeIDE headless sinh lại makefile và build Release.
- Không bật `-ffast-math`; các kiểm tra NaN/Inf vẫn cần cho bảo vệ và Calib.

LTO có thể inline hoặc gộp hàm, nên step/debug một số hàm sẽ khác trước.
Báo cáo stack/độ phức tạp được tạo tại bước link, ở các file
`Debug/Charger_Protection_v2.elf.ltrans*.ltrans.su` / `.cyclo`
và tương ứng trong Release. Báo cáo cũ theo từng object không còn là báo cáo
của mã cuối sau LTO. Không giảm phần stack/heap dự trữ.

CubeIDE 1.18.1 vẫn sinh các target phụ `.su` / `.cyclo` trong rule compile,
kể cả khi đã tắt hai tùy chọn báo cáo theo object. Khi sinh lại makefile,
GNU make có thể báo `pattern recipe did not update peer target` vì GCC chỉ
tạo báo cáo cuối tại bước link LTO. Đây là cảnh báo của target báo cáo; compile
và link firmware vẫn thành công. Các makefile hiện tại đã sửa rule chỉ tạo
`.o`; cần giữ sửa đổi này nếu muốn build CLI không có các cảnh báo đó sau khi
sinh lại makefile. Báo cáo LTO thực vẫn nằm tại đường dẫn nêu trên.

## Kiểm chứng

Các runner host cũng dùng `-Os -flto`: Calib 812 kiểm tra; Measurement 156;
Protection 7.915 RELEASE và 7.913 DEBUG; tích hợp 1.030 mỗi mode. Tất cả qua.
Đã kiểm tra API Calib_Save được giữ và vector SysTick/DMA trỏ tới handler
thực trong ELF ARM. Scheduler: 8256, routing RTE: 6 kiểm tra đều qua, bao gồm
port lấy Calib và xử lý lỗi ứng dụng của Measurement. Cần xác nhận thời gian
ISR và hoạt động trên board. Python có 12 bài test codec/giao thức, liên kết
với mã com/Calib/Rte thực trên host; 2 bài test giao diện tính hệ số/lưu/reset
chạy với cùng mô phỏng MCU. Bản .exe được đóng gói và kiểm tra khởi tạo Tk.
