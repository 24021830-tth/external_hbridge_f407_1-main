# Hướng dẫn sử dụng và bàn giao dự án `external_hbridge_f407`

Tài liệu này mô tả cách build, nạp, nối dây, kiểm tra dữ liệu UART, giải mã DC3-W2 và điều khiển bốn motor trên board STM32F407.

## 1. Tổng quan hoạt động

Luồng xử lý hiện tại:

```text
Thiết bị/app điều khiển
        |
        | TTL UART 115200 8N1
        v
USART2 RX (PD6)
        |
        v
USART2_IRQHandler()
        |
        +--> Log toàn bộ byte HEX [USART2 ANY]
        |
        +--> Dc3w2_DecoderFeed()
                    |
                    +--> Frame sai: bỏ qua điều khiển, vẫn log raw
                    |
                    +--> Frame đúng: giải mã CONTROL
                                      |
                                      v
                              VehicleControl
                                      |
                                      v
                              Motor BTS M1..M4

USART6 TX (PC6) --> USB-TTL --> Serial Monitor trên máy tính
```

USART2 được nhận bằng interrupt. Task FreeRTOS chỉ lấy frame đã nhận từ hàng đợi, log và cập nhật lệnh motor. Vì vậy việc log hoặc xử lý chậm trên USART6 không trực tiếp làm mất từng byte USART2.

## 2. Cấu trúc các file quan trọng

| File | Chức năng |
|---|---|
| `Core/Src/main.c` | Khởi tạo clock, GPIO, timer, USART2, USART6 và FreeRTOS task |
| `Core/Inc/dc3w2_protocol.h` | Định nghĩa frame DC3-W2, hằng số và API decoder |
| `Core/Src/dc3w2_protocol.c` | Đồng bộ `A5 5A`, đọc header, kiểm tra độ dài, CRC và encode frame |
| `Core/Inc/app_control.h` | Kiểu snapshot và API điều khiển ứng dụng |
| `Core/Src/app_control.c` | Nhận USART2, decoder, raw log, PROBE, ACK, kiểm tra CONTROL |
| `Core/Src/vehicle_control.c` | Trộn và phân phối lệnh trái/phải đến bốn motor |
| `Core/Src/motor_bts.c` | PWM, INH và chiều A/B của mạch cầu BTS |
| `Core/Src/usart6_log.c` | Log text không blocking qua USART6 bằng TXE interrupt |
| `Core/Src/stm32f4xx_it.c` | `USART2_IRQHandler()` và `USART6_IRQHandler()` |
| `external_hbridge_f407.ioc` | Cấu hình phần cứng CubeMX gốc |
| `CMakeLists.txt` | Danh sách source được đưa vào firmware |

Không chép nguyên `main.c` của board cũ vào dự án này. Board cũ dùng STM32F1, HAL và PA2/PA3; board hiện tại dùng STM32F407, LL, FreeRTOS và PA2/PA3 đã là PWM của M4.

## 3. Kết nối phần cứng

### USART2: app điều khiển

| Chức năng | Chân STM32F407 | AF |
|---|---:|---:|
| USART2 TX | PD5 | AF7 |
| USART2 RX | PD6 | AF7 |

Kết nối TTL:

```text
TX của thiết bị/app  --> PD6 (USART2_RX)
RX của thiết bị/app  <-- PD5 (USART2_TX, dùng để nhận ACK)
GND hai mạch         --- GND chung
```

Không nối TX của thiết bị vào PA3. PA3 trên board hiện tại là `TIM2_CH4`, dùng cho `M4_IN_B_PWM`.

### USART6: log ra máy tính

| Chức năng | Chân STM32F407 | AF |
|---|---:|---:|
| USART6 TX | PC6 | AF8 |
| USART6 RX | PC7 | AF8 |

Chỉ cần dùng chiều log:

```text
PC6 (USART6_TX) --> RX của USB-TTL
GND STM32       --- GND USB-TTL
```

Cấu hình terminal: `115200 baud`, `8 data bits`, `no parity`, `1 stop bit`, `no flow control`.

## 4. Sơ đồ motor hiện tại

| Motor | PWM chiều A | PWM chiều B | INH A | INH B |
|---|---|---|---|---|
| M1 | TIM1_CH1 / PE9 | TIM1_CH2 / PE11 | PE5 | PE3 |
| M2 | TIM1_CH3 / PE13 | TIM1_CH4 / PE14 | PB9 | PB8 |
| M3 | TIM2_CH1 / PA5 | TIM2_CH2 / PB3 | PC4 | PC5 |
| M4 | TIM2_CH3 / PA2 | TIM2_CH4 / PA3 | PB0 | PB1 |

Mapping trong `vehicle_control.c`:

```text
M1, M2 = phía phải  = right
M3, M4 = phía trái  = left
```

Ví dụ `left=700`, `right=300`:

```text
direction=RIGHT
M1=300, M2=300, M3=700, M4=700
```

Trong `motor_bts.c`, lệnh dương dùng chiều A, lệnh âm dùng chiều B. Khi đổi chiều, motor được dừng trước một lần gọi; frame kế tiếp mới cấp PWM theo chiều mới. App nên gửi lệnh liên tục, không chỉ gửi một frame duy nhất.

## 5. Định dạng frame DC3-W2

```text
[A5] [5A] [VERSION] [TYPE] [FLAGS]
[SEQ: 4 byte little-endian]
[LENGTH: 2 byte little-endian]
[PAYLOAD]
[CRC: 2 byte little-endian]
```

Các hằng số hiện tại nằm trong `Core/Inc/dc3w2_protocol.h`:

```c
#define DC3W2_SYNC_A       0xA5U
#define DC3W2_SYNC_B       0x5AU
#define DC3W2_VERSION      1U
#define DC3W2_MAX_PAYLOAD  64U
```

CRC là `CRC-16/CCITT-FALSE`:

```text
CRC khởi tạo = 0xFFFF
Polynomial   = 0x1021
Phạm vi      = VERSION đến hết PAYLOAD
CRC truyền   = byte thấp trước, byte cao sau
```

### Frame CONTROL

`TYPE = 0x01`, độ dài payload bắt buộc là 5 byte:

| Byte payload | Ý nghĩa | Kiểu |
|---:|---|---|
| 0..1 | `left_permille` | `int16 little-endian`, -1000..1000 |
| 2..3 | `right_permille` | `int16 little-endian`, -1000..1000 |
| 4 | `brake` | 0 cho phép chạy, 1 dừng |

Lưu ý: dự án hiện tại không dùng payload cũ dạng `throttle + steering + enable + flags`. Payload điều khiển hiện tại là `left + right + brake`.

### PROBE và ACK

Theo logic của `main.c` cũ, trước CONTROL cần gửi:

```text
TYPE PROBE = 0x10
payload[0] = 0x01  -> PI
payload[0] = 0x02  -> LAPTOP
```

ACK có:

```text
TYPE ACK = 0x7E
payload[0] = loại frame được phản hồi
payload[1] = status
payload[2] = port ID, USART2 = 2
```

Status hiện tại:

```text
0 = OK
1 = sai loại frame, độ dài hoặc chưa PROBE role PI
2 = sai dữ liệu hoặc sequence lặp/lùi
```

Frame PROBE role PI mẫu, sequence 1:

```text
A5 5A 01 10 00 01 00 00 00 01 00 01 74 B8
```

Frame CONTROL mẫu: `left=700`, `right=300`, `brake=0`, sequence 2:

```text
A5 5A 01 01 00 02 00 00 00 05 00 BC 02 2C 01 00 97 C6
```

## 6. Cấu hình hai luồng nhận dữ liệu

Các macro nằm ở đầu `Core/Src/app_control.c`:

```c
#define APP_CONTROL_ENABLE_VALID_FRAME_PATH  1U
#define APP_CONTROL_ENABLE_RAW_BYTE_PATH     1U
#define APP_CONTROL_REQUIRE_PI_PROBE         1U
```

Ý nghĩa:

| Cấu hình | Kết quả |
|---|---|
| `valid=1`, `raw=1` | Nhận mọi byte để log và đồng thời giải mã frame đúng |
| `valid=1`, `raw=0` | Chỉ log frame DC3W2 hợp lệ và điều khiển motor |
| `valid=0`, `raw=1` | Chỉ log byte HEX, không giải mã, không chạy motor |
| `valid=0`, `raw=0` | Nhận byte nhưng không có log/điều khiển, không dùng để debug |

Cấu hình hiện tại là `valid=1`, `raw=1`, nên frame đúng sẽ có log `[DC3W2 DECODED]`, `[USART2 RX]` và `[VEHICLE]`; dữ liệu sai vẫn có log `[USART2 ANY]` nhưng không được đưa vào motor.

Nếu app không gửi PROBE mà gửi thẳng CONTROL, tạm đổi:

```c
#define APP_CONTROL_REQUIRE_PI_PROBE 0U
```

Khi debug raw, log được gom tối đa 64 byte hoặc flush sau khoảng 100 ms. Vì vậy một dòng `[USART2 ANY]` không nhất thiết bắt đầu đúng đầu frame.

## 7. Dạng log USART6

Khi khởi động:

```text
USART6 LOG READY
```

Dữ liệu raw:

```text
[USART2 ANY] A5 5A 01 01 00 02 00 00 00 05 00 BC 02 2C 01 00 97 C6
```

Frame đã giải mã:

```text
[DC3W2 DECODED] version=1 type=1 flags=0x00 seq=2 payload_length=5 payload=BC 02 2C 01 00
```

Lệnh điều khiển:

```text
[USART2 RX] seq=2 left=700 right=300 direction=RIGHT brake=0 flags=0x00
```

Lệnh motor:

```text
[VEHICLE] direction=RIGHT left=700 right=300 brake=0 M1_cmd=300 M2_cmd=300 M3_cmd=700 M4_cmd=700
```

Dữ liệu sai sẽ không xuất `[VEHICLE]`. Đây là chủ ý an toàn: chỉ frame hợp lệ, đúng dữ liệu và đúng sequence mới được chạy motor.

## 8. Quy trình sử dụng bình thường

1. Kiểm tra nối `TX app -> PD6`, `PD5 -> RX app` nếu cần nhận ACK, và nối chung GND.
2. Nối `PC6 -> RX USB-TTL` để xem log.
3. Mở terminal USART6 ở `115200 8N1`.
4. Cấp nguồn board và kiểm tra dòng `USART6 LOG READY`.
5. App gửi PROBE role PI.
6. App gửi CONTROL lặp lại định kỳ, khuyến nghị 20..50 Hz.
7. Kiểm tra `[DC3W2 DECODED]`, sau đó `[USART2 RX]` và `[VEHICLE]`.
8. Khi dừng, gửi `brake=1` hoặc ngừng gửi quá `100 ms`; hệ thống sẽ dừng motor.

## 9. Build dự án

Nếu máy có CMake và Ninja:

```powershell
cmake --preset Debug
cmake --build --preset Debug --parallel 4
```

Trong môi trường hiện tại có thể dùng:

```powershell
& 'C:\Program Files\MATLAB\R2025b\bin\win64\cmake\bin\cmake.exe' --build build\Debug --parallel 4
```

Output chính:

```text
build\Debug\external_hbridge_f407.elf
build\Debug\external_hbridge_f407.map
```

Build thành công chưa có nghĩa là firmware đã nằm trên chip. Cần dùng STM32CubeProgrammer, ST-LINK hoặc công cụ nạp SWD để nạp file `.elf` vào STM32F407.

Sau khi sửa `CMakeLists.txt`, thêm file mới hoặc đổi cấu hình build, có thể xóa riêng thư mục build Debug rồi configure lại; không xóa mã nguồn trong `Core`.

## 10. Nạp firmware bằng ST-LINK

1. Cấp nguồn đúng cho board.
2. Nối `SWDIO`, `SWCLK`, `GND`, `3V3` và thường nên nối thêm `NRST`.
3. Đảm bảo `BOOT0` ở mức thấp để chạy flash bình thường.
4. Mở STM32CubeProgrammer, chọn ST-LINK và bấm Connect.
5. Chọn `build/Debug/external_hbridge_f407.elf`.
6. Chọn Download/Program, bật Verify nếu có.
7. Reset board rồi kiểm tra log `USART6 LOG READY`.

Nếu CubeProgrammer không thấy ST-LINK, đó là lỗi kết nối/driver/nạp, không phải lỗi giải mã UART.

## 11. Bảng lỗi và cách sửa

### Không có `USART6 LOG READY`

Kiểm tra:

- Firmware mới đã thực sự được nạp chưa.
- USB-TTL đã nối RX vào `PC6`, không nối vào PC7.
- USB-TTL và STM32 có chung GND.
- Terminal đang ở `115200 8N1`.
- USART6 không bị đổi chân trong `main.c` hoặc file `.ioc`.

### Có log USART6 nhưng không có dữ liệu USART2

Kiểm tra:

- TX của thiết bị phải nối vào `PD6`, không phải PD5.
- Có chung GND.
- Thiết bị phát đúng `115200 8N1`.
- Macro `APP_CONTROL_ENABLE_RAW_BYTE_PATH` đang là `1U`.
- Thiết bị thực sự đang gửi dữ liệu sau khi board khởi động.

### Chỉ thấy `[USART2 ANY]`, không có `[DC3W2 DECODED]`

Đây là dấu hiệu byte đã vào USART2 nhưng chưa tạo được frame hợp lệ. Kiểm tra:

- Raw phải có chuỗi bắt đầu `A5 5A` liên tiếp.
- Byte version phải là `01`.
- Length phải đúng và không lớn hơn 64.
- CRC phải được tính từ VERSION đến hết PAYLOAD.
- CRC truyền theo thứ tự byte thấp trước.
- Baudrate, dây RX, mức TTL và GND.

Nếu raw chỉ có `A5`, `EA`, `17`, ... rời rạc mà không có `A5 5A`, ưu tiên kiểm tra UART và nguồn dữ liệu trước khi sửa decoder.

### Có `[DC3W2 DECODED]` nhưng không có `[VEHICLE]`

Kiểm tra:

- Đã nhận `PROBE role=PI` chưa.
- `type` có phải `1` không.
- `payload_length` có đúng 5 không.
- Left/right có nằm trong -1000..1000 không.
- `brake` có bằng 0 không.
- `seq` có tăng dần không.
- Nếu app không gửi PROBE, đổi `APP_CONTROL_REQUIRE_PI_PROBE` thành `0U`.

### Có `[VEHICLE]` nhưng motor dừng ngay

Kiểm tra:

- App phải gửi CONTROL lặp lại nhanh hơn timeout 100 ms.
- `brake` phải bằng 0 khi muốn chạy.
- Không có log `UART error` hoặc `RX queue overrun`.
- Khi đổi chiều, lệnh đầu tiên có thể chỉ làm motor dừng; lệnh kế tiếp mới chạy chiều mới.

### Motor chạy ngược hoặc trái/phải bị đảo

Không đổi định dạng frame trước. Kiểm tra lần lượt:

1. `vehicle_control.c`: M1/M2 đang là phải, M3/M4 đang là trái.
2. `motor_bts.c`: PWM A/B của motor có đúng dây BTS không.
3. Nếu chỉ một motor ngược, đổi hai đường PWM A/B của motor đó trong `motor_config` hoặc đổi dây tại mạch.
4. Không đổi PA2/PA3 sang USART2 vì chúng đang điều khiển M4.

### Báo `UART error: decoder reset`

USART2 đã báo ORE/FE/NE. Kiểm tra baudrate, clock, mức điện áp TTL và nhiễu dây. Decoder được reset có chủ ý vì frame đang nhận đã không còn đáng tin cậy.

### Báo `RX queue overrun: frame discarded`

Task không xử lý kịp tốc độ nhận hoặc app gửi quá dày. Kiểm tra log USART6 có quá nhiều raw không; có thể tắt raw bằng:

```c
#define APP_CONTROL_ENABLE_RAW_BYTE_PATH 0U
```

Không tăng log trong interrupt. Nếu cần, tăng `APP_CONTROL_RX_QUEUE_SIZE` hoặc giảm tần số gửi CONTROL.

### Build lỗi `undefined reference` hoặc không tìm thấy file

Kiểm tra:

- File `.c` đã có trong `target_sources()` của `CMakeLists.txt` chưa.
- Header có nằm trong `Core/Inc` chưa.
- Không khai báo trùng `USART2_IRQHandler()` hoặc `USART6_IRQHandler()` ở nhiều file.
- Reconfigure CMake sau khi thêm file mới.

### Không nạp được firmware

Build chỉ kiểm tra code; nó không kiểm tra kết nối chip. Kiểm tra ST-LINK driver, dây SWD, nguồn 3.3 V, BOOT0, NRST và đảm bảo không có chương trình khác đang giữ ST-LINK.

## 12. Quy tắc khi bàn giao hoặc tiếp tục sửa code

- Giữ nguyên chân USART2 hiện tại: `PD5/PD6`; không lấy lại PA2/PA3 của board cũ.
- Nếu CubeMX regenerate code, kiểm tra lại `main.c`, `stm32f4xx_it.c` và danh sách source trong `CMakeLists.txt`.
- Không gọi hàm log blocking hoặc hàm motor nặng trong `USART2_IRQHandler()`.
- Decoder chỉ chấp nhận frame có sync, version, length và CRC đúng.
- Raw log dùng để chẩn đoán byte UART; không dùng raw byte để điều khiển motor.
- Chỉ frame CONTROL hợp lệ mới cập nhật `AppControl_Snapshot_t`.
- Motor phải dừng khi frame hết hạn, brake bật, UART lỗi hoặc dữ liệu không hợp lệ.
- Khi thay đổi format payload, phải sửa đồng thời tài liệu, `app_control.h`, app phát dữ liệu và test CRC.

