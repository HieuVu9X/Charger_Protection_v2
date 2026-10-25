# Lưu hệ số calib trong một trang Flash

Lưu gain Vin, Vout, Iin, Ileak và offset nhiệt độ (°C). Gain hợp lệ
0,25..4; offset -20..20 °C. NaN/Inf bị từ chối. Các gain nhân thêm vào hệ số
phần cứng/factory trim trong Measurement.h. Mặc định: gain 1, offset 0.

Gain 1 ở đây chỉ có nghĩa là chưa thêm hiệu chỉnh sai số của từng board.
Hệ số chuyển đổi theo tỷ lệ cầu chia, điện trở shunt, CT và gain khuếch đại
vẫn dùng như trước khi thêm Calib. Khi chưa calib, kết quả đo dùng các hệ số
phần cứng danh định đó và chấp nhận sai số linh kiện trên mạch. Khi đã calib,
kết quả bằng giá trị tính theo phần cứng nhân thêm gain bù đã đo được.

## Vùng lưu

- Chỉ dùng trang 15: `0x08003C00..0x08003FFF`, đúng 1 KB.
- Linker dành 15 KB đầu cho firmware, 1 KB cuối cho CALIB. Trang 14 đã trả
  lại cho code. CALIB không chứa dữ liệu khởi tạo trong ELF.
- Một record version 2, 80 byte: magic, version, size, sequence, Calib 20 byte,
  Protection 40 byte, CRC-32/ISO-HDLC, commit và reserved. sequence là số lần cập nhật,
  không dùng để chọn giữa hai trang.
- Khởi động kiểm tra record duy nhất. Dữ liệu trắng, sai định dạng, ngoài giới
  hạn, CRC lỗi hoặc chưa có commit đều dẫn tới hệ số mặc định.
- Ghi mới xóa chính trang này, ghi nội dung/CRC, đọc lại kiểm tra rồi ghi
  commit cuối cùng. Không có bản dự phòng: mất nguồn/ghi lỗi sau lúc xóa có
  thể mất bộ cũ; lần khởi động sau dùng mặc định nếu record chưa hợp lệ.
- Nếu mất nguồn sau khi commit hoàn tất nhưng trước khi trả kết quả, lần
  khởi động sau vẫn nhận bộ mới đã hợp lệ.
- Không ghi tự động lúc khởi động. Bộ tham số trùng bản đã lưu trả về
  `CALIB_UNCHANGED`, không xóa/ghi thêm.

Nếu nâng cấp từ bản hai trang, chỉ record đang ở trang 15 còn được đọc;
bản ở trang 14 không được di chuyển tự động. Sao lưu/nạp lại hệ số nếu cần.

Firmware mới đọc được record version 1 (40 byte): giữ Calib cũ, dùng ngưỡng
Protection mặc định. Không tự ghi/chuyển đổi lúc khởi động. Lần lưu thay đổi
tiếp theo ghi version 2; nhóm tham số còn lại được giữ theo bản đã lưu, không
lấy từ bộ active cũ khi đang chờ reset. Firmware cũ không đọc được version 2.
CRC/commit bảo vệ chung cả hai nhóm. Record lỗi thì cả hai dùng mặc định.
Giữ lại tham số khi nâng cấp bằng cách chỉ erase vùng firmware.

## API và luồng

`main.c` gọi `Calib_Init()` trước `MeasurementInit()`. Measurement lấy hệ số
qua `Rte_Call_Calib_Get()` trong Rte.h; không include trực tiếp Calib.h hoặc
gọi trực tiếp Calib_Get(). RTE cung cấp cả kiểu CalibParams_t cho client.
Hệ số áp dụng cho RMS, công suất thực/biểu kiến và ngưỡng dòng rò nhanh;
offset nhiệt độ không che mất lỗi NTC.

```c
#include "Calib.h"

CalibParams_t params = *Calib_Get();
params.vin_gain = 1.015f; /* Ví dụ; lấy từ phép đo chuẩn trên board. */
CalibStatus_t status = Calib_Save(&params);
/* Gửi kết quả về PC, reset MCU sau khi lưu thành công để áp dụng. */
```

Ví dụ trên dành cho bên quản lý calib. Client Measurement dùng port RTE.
`Calib_Get()` trả về bộ đang áp dụng trong RAM của lần khởi động hiện tại.
`Calib_Save()` lưu cho lần khởi động sau, không đổi trực tiếp kết quả đo.
Gain mới được tính bằng `gain_cu * gia_tri_chuan / gia_tri_dang_do` với số đo
ổn định, khác 0, không clipping. Offset nhiệt độ mới bằng offset cũ cộng
`T_chuan - T_dang_do`.

| Trạng thái | Ý nghĩa |
| --- | --- |
| `CALIB_OK` | Đã ghi và đọc lại xác nhận record hợp lệ |
| `CALIB_UNCHANGED` | Trùng bản đã lưu, không ghi |
| `CALIB_INVALID` | NULL/tham số ngoài giới hạn |
| `CALIB_BUSY` | Gọi từ ISR, đang khóa IRQ hoặc Flash đang bận |
| `CALIB_FLASH_ERROR` | Ghi/xác minh lỗi; bộ cũ có thể đã bị xóa |

Chỉ gọi SAVE từ main loop sau khi khởi tạo GPIO/bảo vệ, IRQ đang bật. Khi
cần ghi thật, firmware mở hai relay, khóa tự đóng lại, dừng TIM1/ADC/DMA rồi
khóa IRQ trong lúc ghi Flash. Sau khi trả kết quả, relay và lấy mẫu vẫn giữ
trạng thái này tới khi MCU reset, kể cả ghi lỗi. INVALID/UNCHANGED/BUSY không
bắt đầu chế độ service mới. Không dùng ProtectionInit/MeasurementInit để
thoát service khi chưa reset.

Module `com` hiện đã cung cấp các lệnh đọc/lưu Calib và RESET qua UART;
xem [giao thức](../com/README.md) và [tool Python](../../Tools/README.md).
UART phải đợi kết quả SAVE trước khi gửi tiếp. Các API mới dùng qua RTE:

- `Calib_GetInfo()` đọc nguồn hệ số đang chạy, tính hợp lệ/sequence của bản
  trong Flash và cờ `reset_required`.
- `Calib_ReadStored()` đọc bản trong Flash, trả INVALID khi không hợp lệ.
- `Calib_RestoreDefaults()` lưu `{1,1,1,1,0}` bằng cùng luồng SAVE.

`reset_required` được giữ sau khi bắt đầu ghi thật, cả khi ghi lỗi, và chỉ xóa
khi khởi động lại. `active_from_flash` phản ánh nguồn lúc boot; không đổi khi
lưu bộ mới. `stored_valid` phản ánh bản hiện có trong Flash.

Mass erase/full-chip erase vẫn xóa calib. Khi nạp firmware muốn giữ tham số,
chỉ xóa các trang firmware. Giữ giới hạn Flash 15 KB và các symbol CALIB khi
thay linker hoặc sinh lại project.

## Kiểm chứng

- Calib: 812 kiểm tra với đúng 1 KB Flash giả lập, gồm CRC chuẩn, tham số lỗi,
  ghi lặp, hỏng từng bit, version/size sai, sequence wrap, lỗi đọc lại và mất
  nguồn trước/sau xóa và từng halfword. Kiểm tra đúng việc dùng mặc định khi
  mất record sau xóa và dùng bản cũ chỉ khi việc xóa chưa bắt đầu.
- Measurement: 156; Protection: 7915 RELEASE, 7913 DEBUG; tích hợp: 1030 mỗi
  mode; Scheduler: 8256; routing RTE: 6. Tất cả qua.
- Chưa xác nhận ghi Flash và mất nguồn thực trên board.
