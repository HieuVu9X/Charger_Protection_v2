# RTE và Scheduler

Callback giữa các component đi qua macro `#define` trong `Rte.h`, áp dụng cho Measurement → Protection và các lời gọi từ com. `Rte.c` giữ dữ liệu dùng chung (`MeasureResult`) và hàm sao chép snapshot `Rte_Read_MeasureResult()`. Khởi tạo component tại `main.c`, các `MainFunction` trong task scheduler, hai hàm xử lý ADC DMA và `Error_Handler()` được gọi trực tiếp theo yêu cầu của dự án. Các hàm nội bộ cùng một component cũng gọi trực tiếp.

RTE chỉ ánh xạ cổng gọi/sự kiện tới hàm đích, không chứa thuật toán đo hoặc quyết định bảo vệ. Macro được mở rộng trực tiếp thành lời gọi hàm đích khi biên dịch, không tạo thêm hàm wrapper. Lời gọi từ DMA/SysTick vẫn thực thi trong ISR, không chuyển sang task hoặc đưa vào hàng đợi. Đây là cách tổ chức lớp kết nối gợi theo AUTOSAR, không phải một triển khai AUTOSAR đầy đủ.

| Cổng RTE | Đích gọi |
|---|---|
| `Rte_Call_Protection_Tick1msISR()` | `Protection_Tick1msISR()` |
| `Rte_Call_Measurement_ZeroCross()` | `Protection_ZeroCrossISR()` |
| `Rte_Call_Protection_EmergencyStop()` | `Protection_EmergencyStop()` |

Khi thêm callback giữa các component, tạo macro dạng `#define Rte_Call_...() Provider_Function()` trong `Rte.h`, rồi gọi tên RTE từ component nguồn. RTE include header của component đích để khai báo hàm đích có hiệu lực tại nơi macro được sử dụng. Không đặt phần nối callback giữa component trong `main.c`. Component nguồn không cần include trực tiếp header component đích; RTE giữ phần kết nối. Main, scheduler và handler DMA được include header component để thực hiện các lời gọi trực tiếp nêu trên.

Scheduler cộng tác đơn giản trong `Scheduler.h` và `scheduler.c`, dùng ngắt SysTick 1 ms đã được `SystemClock_Config()` cấu hình. Không dùng RTOS hoặc timer mới.

| Task | Chu kỳ yêu cầu | Hàm mặc định |
|---|---|---|
| `Scheduler_Task1ms()` | 1 ms | `Measurement_MainFunction()` rồi `Protection_MainFunction()` |
| `Scheduler_Task10ms()` | 10 ms | `Rte_Call_Com_MainFunction()` trong `scheduler.c` |
| `Scheduler_Task100ms()` | 100 ms | Để trống để thêm component |

Các task nằm cuối `scheduler.c`. Thêm lời gọi `MainFunction` trực tiếp vào task phù hợp. Các hàm task mặc định khai báo `__WEAK`, cũng có thể định nghĩa lại trong source ứng dụng nếu muốn giữ scheduler riêng. Callback giữa component vẫn đi qua RTE.

Sau khi khởi tạo GPIO/clock và peripheral, `main.c` gọi `Calib_Init()`, `ProtectionInit()`, `MeasurementInit()`, `Com_Init()` rồi `Scheduler_Init()`. Trong `while (1)` chỉ cần gọi `Scheduler_MainFunction()` liên tục. `SysTick_Handler()` gọi `Scheduler_Tick1msISR()`. `DMA1_Channel1_IRQHandler()` gọi trực tiếp `Measurement_DMA_IRQHandler()` rồi `Protection_FastLeakageISR()`; đây là ngắt ADC1 DMA1 Channel 1 với các sự kiện HT/TC/TE. `USART1_IRQHandler()` gọi `Com_UART_IRQHandler()` để chuyển byte, không xử lý lệnh hay ghi Flash trong ISR.

SysTick đánh dấu các task đến hạn, đồng thời gọi `Rte_Call_Protection_Tick1msISR()` để timeout chờ zero crossing 30 ms và LED không phụ thuộc tiến độ vòng lặp chính. Các hàm `MainFunction` chạy trong main loop theo thứ tự 1 ms → 10 ms → 100 ms, với ngắt hoạt động bình thường. Ngắt DMA và callback zero crossing tiếp tục xử lý trong ISR.

Scheduler lấy và xóa cờ đến hạn trong một đoạn khóa ngắt ngắn. Tick xuất hiện trong lúc task đang chạy sẽ được giữ cho lần dispatch kế tiếp. Nếu main loop bận qua nhiều chu kỳ thì gộp các lần đến hạn thành một lần chạy mỗi task; không chạy bù hàng loạt với cùng dữ liệu đo. Vì vậy đây là chu kỳ phát hành task, không bảo đảm thời điểm bắt đầu chính xác khi CPU quá tải. Task cần trả về nhanh, tránh delay hoặc chờ blocking.

`makefile.defs` giúp các makefile CLI Debug/Release hiện có biên dịch và liên kết `scheduler.c`. Sau khi CubeIDE tự tạo lại makefile và nhận source mới, điều kiện trong file này tự tránh liên kết trùng object.

Chạy kiểm thử với `node Core/Rte/tests/run_scheduler_tests.mjs`, GCC native trong PATH hoặc truyền đường dẫn GCC làm đối số thứ nhất. Các file `.c.in`/`.h.in` không được đưa vào firmware.

Kết quả hiện tại có com: Scheduler đạt 8.256 kiểm tra, routing macro RTE đạt 6; các bộ Calib, Measurement, Protection và tích hợp đều qua. Debug/Release build thành công. Flash text+data 12.916 byte / 15.360 byte; RAM data+bss 2.040 byte / 4.096 byte, đã gồm vùng heap/stack tối thiểu do linker dành trước. Chưa đo thời gian thực thi xấu nhất hoặc kiểm tra scheduler trên board.


## Port liên module của Measurement

Measurement dùng Rte_Call_Calib_Get() ở khởi tạo ngưỡng dòng rò nhanh và khi tính kết quả đo; Rte_Call_Application_ErrorHandler() ở nhánh lỗi DMA; Rte_Call_Measurement_ZeroCross() ở callback zero crossing. Rte.h cung cấp khai báo provider và kiểu CalibParams_t. Các port là macro đồng bộ như cấu trúc RTE hiện có, giữ nguyên ngữ cảnh ISR/task và không thêm call overhead.

Measurement không include Calib.h hay gọi trực tiếp Calib_Get()/Error_Handler(). Các API LL truy cập phần cứng và hàm thư viện C vẫn dùng trực tiếp. Kiểm thử routing hiện có 6 kiểm tra, xác nhận cả con trỏ Calib trả về và callback xử lý lỗi; Scheduler có 8256 kiểm tra, tất cả qua.

## Port của com

com dùng `Rte_Call_Calib_Get/GetInfo/ReadStored/Save/RestoreDefaults`,
`Rte_Call_Protection_GetFaultMask`, `Rte_Call_Scheduler_GetTimeMs` và
`RTE_Call_Protection_EnterCalibMode`. `Application_Reset()` nằm trong `com.c`,
được gọi trực tiếp sau khi ACK đã truyền xong, gọi port Protection rồi
`NVIC_SystemReset()`. Dữ liệu đo lấy bằng `Rte_Read_MeasureResult` tại
main loop. Bộ đếm `Scheduler_GetTimeMs()` là uint32 tăng mỗi SysTick; parser
so sánh hiệu unsigned để xử lý wrap. Task 10 ms được triển khai trong
`scheduler.c` và gọi `Rte_Call_Com_MainFunction()`.
