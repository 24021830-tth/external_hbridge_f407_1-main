# Gợi ý thuật toán vi sai điện tử cho xe 4 động cơ

Tài liệu này chỉ mô tả thuật toán, công thức và trình tự triển khai. Chưa thay đổi trực tiếp vào firmware.

## 1. Phạm vi và quy ước

Mapping hiện tại của dự án:

| Nhóm | Động cơ | Timer PWM hiện tại |
|---|---|---|
| Bên phải | M1, M2 | TIM1 CH1/CH2 và CH3/CH4 |
| Bên trái | M3, M4 | TIM3 CH1/CH2 và TIM2 CH3/CH4 |

Quy ước:

- v: vận tốc dọc mong muốn, dương là tiến, âm là lùi.
- omega: tốc độ quay quanh trục đứng, dương là quay trái.
- B: khoảng cách giữa tâm hai dải bánh trái/phải.
- Lệnh động cơ chuẩn hóa trong khoảng -1.0 .. +1.0 hoặc -1000 .. +1000 permille.
- Cần xác nhận chiều A/B thực tế của từng động cơ. Nếu một động cơ bị đảo dây, dùng hệ số dấu riêng s_i = +1 hoặc -1.

Trong code hiện tại, frame CONTROL đã có left_permille và right_permille. Vì vậy cần phân biệt:

1. Nếu app gửi v và omega: firmware phải thực hiện phép trộn vi sai.
2. Nếu app đã gửi lệnh trái/phải: firmware không được trộn lần thứ hai, chỉ thực hiện cân bằng, giới hạn tốc độ, ramp và bảo vệ đổi chiều.

## 2. Sơ đồ xử lý đề xuất

~~~text
Nhận lệnh và kiểm tra CRC
        |
        v
Kiểm tra timeout / brake / giá trị hợp lệ
        |
        v
Deadband và chuẩn hóa joystick
        |
        v
Trộn vi sai: v, omega -> lệnh trái/phải
        |
        v
Giới hạn và chuẩn hóa đồng thời hai bên
        |
        v
Bù lệch trái/phải và bù sai khác từng motor
        |
        v
Ramp tăng/giảm tốc, xử lý đổi chiều an toàn
        |
        v
Đổi lệnh sang CCR, chọn channel A/B, điều khiển INH
~~~

PWM carrier và vòng điều khiển là hai lớp riêng:

- PWM TIM1/TIM2/TIM3 tạo sóng nhanh, hiện khoảng 14 kHz theo ARR của dự án.
- Vòng điều khiển cập nhật lệnh chậm hơn, ví dụ 1–10 ms, và phải dùng dt thực tế.
- TIM6 chỉ nên phục vụ timebase/HAL; không dùng TIM6 để tạo PWM động cơ.

## 3. Công thức vi sai điện tử

Với xe vi sai lý tưởng:

~~~text
u_left  = v - omega * B / 2
u_right = v + omega * B / 2
~~~

Sau đó chuẩn hóa nếu một giá trị vượt biên:

~~~text
scale = max(1, abs(u_left), abs(u_right))
u_left  = u_left  / scale
u_right = u_right / scale
~~~

Chia cả hai bên cho cùng một scale giúp giữ nguyên bán kính cua, tránh trường hợp một bên bị cắt đỉnh làm xe đổi hướng ngoài ý muốn.

Lệnh cuối cùng cho từng motor có thể thêm hệ số hiệu chỉnh:

~~~text
cmd_M1 = s_M1 * gain_M1 * u_right
cmd_M2 = s_M2 * gain_M2 * u_right
cmd_M3 = s_M3 * gain_M3 * u_left
cmd_M4 = s_M4 * gain_M4 * u_left
~~~

Giai đoạn đầu nên đặt các gain bằng nhau. Chỉ thêm hiệu chỉnh sau khi đã xác nhận đúng dây, đúng chiều và đúng mapping.

## 4. Các trường hợp chuyển động

Trong bảng dưới đây, p là mức tiến/lùi và q là mức cua. Ví dụ giả sử q < p để hai bên vẫn cùng chiều.

| Trạng thái | v | omega | Bên trái | Bên phải |
|---|---:|---:|---:|---:|
| Đi thẳng tiến | +p | 0 | +p | +p |
| Tiến rẽ trái | +p | +q | p-q | p+q |
| Tiến rẽ phải | +p | -q | p+q | p-q |
| Lùi rẽ trái theo yaw | -p | +q | -p-q | -p+q |
| Lùi rẽ phải theo yaw | -p | -q | -p+q | -p-q |
| Quay trái tại chỗ | 0 | +q | -q | +q |
| Quay phải tại chỗ | 0 | -q | +q | -q |

Quay tại chỗ:

- Quay trái: bên trái chạy lùi, bên phải chạy tiến.
- Quay phải: bên trái chạy tiến, bên phải chạy lùi.

Nếu giao diện người dùng định nghĩa joystick trái/phải khác với omega, cần đảo dấu omega ở một chỗ duy nhất, tránh đảo dấu rải rác trong từng trường hợp.

## 5. Deadband và tránh giật khi bắt đầu chạy

Joystick hoặc dữ liệu ADC thường có nhiễu quanh vị trí 0. Dùng deadband:

~~~text
if abs(x) < deadband:
    x = 0
else:
    x = sign(x) * (abs(x) - deadband) / (1 - deadband)
~~~

Giá trị khởi đầu có thể thử deadband = 0.03 .. 0.08, sau đó chỉnh theo thiết bị.

Không đưa ngay lệnh từ 0 lên giá trị lớn. Dùng giới hạn tốc độ thay đổi:

~~~text
delta       = target - current
delta_limit = acceleration_limit * dt
current     = current + clamp(delta, -delta_limit, +delta_limit)
~~~

Nên có hai giới hạn:

- acceleration_limit: tăng tốc tối đa.
- deceleration_limit: giảm tốc tối đa; có thể lớn hơn tăng tốc nhưng không được gây trượt hoặc quá dòng.

Nếu động cơ có vùng không thắng được ma sát tĩnh, có thể dùng start_duty. Mức này phải được đưa vào qua ramp:

~~~text
abs(cmd) > 0 -> ramp tới start_duty trước
             -> sau đó ramp tiếp tới duty yêu cầu
~~~

Không nên dùng start_duty quá lớn vì sẽ tạo cú giật lúc khởi động.

## 6. Đổi chiều an toàn

Không đổi trực tiếp từ +duty sang -duty. Mỗi motor hoặc mỗi bên nên có state machine:

~~~text
RUN_FORWARD
RUN_REVERSE
RAMP_TO_ZERO
INHIBIT_WAIT
RAMP_FROM_ZERO
~~~

Trình tự khi phát hiện lệnh đổi dấu:

1. Giữ nguyên chiều cũ, giảm duty theo deceleration_limit về 0.
2. Khi duty bằng 0, đưa cả hai chân INH về trạng thái disable.
3. Đặt CCR của cả hai channel motor về 0.
4. Chờ thời gian chết T_reverse_deadtime.
5. Cập nhật trạng thái chiều mới.
6. Giữ INH disable trong lúc chuẩn bị channel mới.
7. Ramp duty từ 0 lên giá trị mới.
8. Chỉ bật INH sau khi channel không hoạt động có CCR bằng 0 và channel hoạt động đã được nạp đúng giá trị.

T_reverse_deadtime phải được thử nghiệm theo mạch BTS, tải và quán tính xe. Có thể bắt đầu trong khoảng 50 .. 200 ms, nhưng đây chỉ là giá trị gợi ý, không thay thế yêu cầu datasheet.

Đối với lệnh quay tại chỗ, hai bên có thể đổi chiều ngược nhau. Vẫn phải xử lý đổi chiều riêng cho từng bên, không bỏ qua dead-time.

### Dừng thường và dừng khẩn cấp

- Dừng thường: ramp duty về 0, sau đó disable INH.
- Mất frame/timeout: ramp về 0 trong thời gian ngắn rồi disable INH.
- Brake hoặc lỗi quá dòng: disable INH ngay lập tức, xóa CCR; chỉ cho chạy lại sau khi điều kiện an toàn được khôi phục.

Việc disable INH phải xảy ra trước khi thay đổi các CCR đang điều khiển cầu công suất.

## 7. Hai motor cùng một bên

M1/M2 cùng nhận lệnh bên phải; M3/M4 cùng nhận lệnh bên trái. Không nên giả định hai motor hoàn toàn giống nhau.

Quy trình hiệu chỉnh:

1. Nâng xe khỏi mặt đất và kiểm tra từng motor ở duty thấp.
2. Xác nhận hai motor cùng bên quay cùng chiều cơ học.
3. Đặt gain_M1 .. gain_M4 bằng nhau.
4. Đo tốc độ hoặc quãng đường từng bên.
5. Chỉ hiệu chỉnh nhỏ, ví dụ gain = 0.95 .. 1.05.
6. Giới hạn tổng lệnh sau hiệu chỉnh một lần nữa.

Nếu hai motor cùng bên chạy ngược chiều cơ học do cách lắp đối xứng, chỉ đảo dấu motor bị ngược bằng s_i.

## 8. Đi thẳng không bị lệch trái/phải

### 8.1. Không có encoder

Dùng bù lệch cố định:

~~~text
u_left  = clamp(u_straight + trim, -1, +1)
u_right = clamp(u_straight - trim, -1, +1)
~~~

Quy ước: trim dương làm bên trái mạnh hơn tương đối.

- Xe lệch trái: thử tăng trim dương.
- Xe lệch phải: thử trim âm.

Trim cố định chỉ phù hợp khi tải, điện áp pin và mặt đường tương đối ổn định.

### 8.2. Có encoder

Với lệnh thẳng, đặt cùng tốc độ mục tiêu cho hai bên:

~~~text
error_left  = speed_target_left  - speed_measured_left
error_right = speed_target_right - speed_measured_right
~~~

Bộ điều khiển PI:

~~~text
u[k] = u[k-1] + Kp * (error[k] - error[k-1]) + Ki * error[k] * dt
~~~

Cần có:

- Giới hạn đầu ra PI.
- Anti-windup cho tích phân.
- Lọc tốc độ đo.
- Giới hạn gia tốc trước khi đưa vào PI.
- Có thể thêm vòng yaw bằng gyro nếu cần giữ hướng tốt hơn.

## 9. Công thức chuyển lệnh sang CCR

Với lệnh chuẩn hóa u:

~~~text
duty_percent = 100 * abs(u)
CCR_active    = round(duty_percent * (ARR + 1) / 100)
CCR_inactive  = 0
~~~

Chiều dấu quyết định channel hoạt động:

~~~text
u >= 0 -> channel A active, channel B = 0
u <  0 -> channel B active, channel A = 0
u == 0 -> cả hai CCR = 0, INH disable
~~~

Khi triển khai thực tế nên giới hạn CCR_active không vượt quá giá trị hợp lệ của timer. Quy ước dùng ARR hay ARR+1 phải thống nhất cho TIM1, TIM2 và TIM3.

Với cấu hình hiện tại:

- TIM1 có ARR = 11999, dùng cho M1/M2.
- TIM2 và TIM3 có ARR = 5999, dùng cho M4/M3.

## 10. Pseudocode mức thuật toán

~~~text
control_update(input, dt):
    if input_invalid or command_timeout:
        target_left  = 0
        target_right = 0
        emergency_or_normal_stop = true
    else:
        v     = apply_deadband(input.throttle)
        omega = apply_deadband(input.steering)

        target_left  = v - omega * wheelbase / 2
        target_right = v + omega * wheelbase / 2
        normalize_pair(target_left, target_right)

        apply_side_trim_and_motor_gains()

    for motor in M1..M4:
        target = target_for_motor(motor)

        if emergency_stop:
            inhibit_motor(motor)
            clear_both_ccr(motor)
        else if sign(target) != sign(current[motor]) and
                abs(current[motor]) > zero_threshold:
            ramp_current_to_zero(motor, dt)
            if current[motor] == 0:
                inhibit_motor(motor)
                clear_both_ccr(motor)
                start_deadtime_timer(motor)
        else if deadtime_finished(motor):
            current[motor] = slew_limit(current[motor], target, dt)
            write_inactive_ccr_zero(motor)
            write_active_ccr(motor, abs(current[motor]))
            enable_inh_after_ccr_ready(motor)
~~~

Pseudocode chỉ mô tả thứ tự. Khi đưa vào firmware cần bảo đảm state machine không bị gọi đồng thời từ nhiều task hoặc ISR.

## 11. Tham số cần đặt và hiệu chỉnh

| Tham số | Ý nghĩa | Cách bắt đầu |
|---|---|---|
| deadband | Bỏ nhiễu quanh 0 | 3–8% |
| start_duty | Duty tối thiểu thắng ma sát | Đo thực tế, bắt đầu thấp |
| acceleration_limit | Giới hạn tăng tốc | Tăng dần đến khi xe không giật |
| deceleration_limit | Giới hạn giảm tốc | Không làm trượt bánh |
| T_reverse_deadtime | Thời gian chờ khi đổi chiều | Thử 50–200 ms |
| trim | Bù lệch khi đi thẳng | Chỉnh theo hướng xe lệch |
| gain_M1..M4 | Bù khác nhau giữa motor | Chỉnh nhỏ quanh 1.0 |
| command_timeout | Mất lệnh thì dừng | Lớn hơn chu kỳ frame bình thường |

Không nên hiệu chỉnh nhiều tham số cùng lúc. Thứ tự nên là: chiều motor → mapping → deadband → ramp → trim → gain → PI/gyro.

## 12. Kế hoạch kiểm thử an toàn

1. Kiểm tra từng motor khi xe kê cao, duty thấp.
2. Kiểm tra lệnh 0: cả hai CCR của motor bằng 0, INH disable.
3. Kiểm tra tiến thẳng ở 10%, 20%, 30%; xác nhận không giật.
4. Kiểm tra tiến rẽ trái/phải ở tốc độ thấp.
5. Kiểm tra lùi rẽ trái/phải; xác nhận lại quy ước yaw/UI.
6. Kiểm tra quay tại chỗ ở duty thấp.
7. Đang tiến thì đổi sang lùi; xác nhận có đoạn zero + INH disable.
8. Ngắt dữ liệu điều khiển; xác nhận watchdog dừng xe.
9. Tăng tải và thời gian chạy; kiểm tra dòng, nhiệt độ BTS, nguồn và motor.

Trong quá trình test nên ghi lại: lệnh trái/phải, lệnh từng motor, CCR, CCER, trạng thái INH, điện áp pin và dòng motor. Nếu CCR đúng nhưng chân MCU không có xung, kiểm tra AF/GPIO; nếu chân MCU có xung nhưng motor không chạy, kiểm tra INH, dây tín hiệu, nguồn và driver.

## 13. app_control

- Đang đóng vai trò nhận và xử lý lệnh điều khiển từ USART2, sau đó lưu lệnh vào snapshot để task khác có thể sử dụng. 
1. Luồng hoạt động
USART2_IRQHandler
        ↓
AppControl_IrqHandler()
        ↓
Giải mã frame DC3W2
        ↓
AppControl_Process()
        ↓
AppControl_HandleFrame()
        ↓
Cập nhật AppControl_Snapshot
        ↓
VehicleControl_Update()
        ↓
MotorBts_RunCommand()
2. Các hàm và chức năng
| Hàm | Chức năng |
|---|---|
| `AppControl_NowMs()` | Lấy thời gian hiện tại từ FreeRTOS tick |
| `AppControl_IrqHandler()` | Nhận từng byte từ USART2 trong ngắt, kiểm tra lỗi UART và đưa byte vào bộ giải mã |
| `AppControl_Process()` | Được gọi trong FreeRTOS task để lấy frame đã nhận và xử lý |
| `AppControl_HandleFrame()` | Xử lý frame `PROBE` và `CONTROL`, kiểm tra dữ liệu hợp lệ |
| `AppControl_ReadI16LE()` | Đọc giá trị `int16_t` từ 2 byte dữ liệu |
| `AppControl_IsPermilleValid()` | Kiểm tra lệnh có nằm trong khoảng `-1000..1000` không |
| `AppControl_IsNewSequence()` | Kiểm tra số thứ tự frame có mới hơn frame trước không |
| `AppControl_UpdateHealth()` | Kiểm tra timeout kết nối |
| `AppControl_SendAck()` | Gửi phản hồi ACK về bộ điều khiển qua USART2 |
| `AppControl_GetSnapshot()` | Cung cấp lệnh điều khiển mới nhất cho `vehicle_control` |
| `AppControl_Init()` | Khởi tạo decoder, queue, trạng thái và ngắt USART2 |
3. Cách lấy thông số
snapshot.left_permille   // Lệnh bên trái
snapshot.right_permille  // Lệnh bên phải
## 14. vehicle_control

- Nhận giá trị trái/phải, kiểm tra trạng thái an toàn rồi gửi lệnh xuống motor_bts

AppControl_GetSnapshot()
        ↓
VehicleControl_Update()
        ↓
VehicleControl_ApplyCommands()
        ↓
MotorBts_RunCommand() cho M1..M4

| Hàm | Chức năng |
|---|---|
| `VehicleControl_ClampCommand()` | Giới hạn lệnh trong khoảng `-1000..1000` |
| `VehicleControl_ApplyCommands()` | Phân bổ lệnh trái/phải cho 4 động cơ |
| `VehicleControl_LogDue()` | Kiểm tra đã đến thời điểm ghi log 100 ms chưa |
| `VehicleControl_Stop()` | Dừng cả 4 động cơ |
| `VehicleControl_Init()` | Xóa trạng thái log và dừng động cơ lúc khởi tạo |
| `VehicleControl_Update()` | Hàm điều khiển chính |

## 15. motor_bts

- Module điều khiển trực tiếp 4 cầu H BTS bằng GPIO INH và PWM timer. Nhận dữ liệu từ vehicle_control 

| Hàm | Chức năng |
|---|---|
| `MotorBts_PercentToCompare()` | Đổi phần trăm tốc độ thành giá trị CCR dựa trên ARR của timer |
| `MotorBts_SetCompare()` | Ghi giá trị CCR cho CH1, CH2, CH3 hoặc CH4 |
| `MotorBts_StopOneInternal()` | Tắt INH, xóa CCR của một động cơ |
| `MotorBts_SetSpeedOneInternal()` | Chọn kênh PWM theo chiều quay và cấp tốc độ |
| `MotorBts_Stop()` | Xóa CCR của các channel được chỉ định |
| `MotorBts_InitSafe()` | Khởi tạo trạng thái an toàn cho GPIO, timer, PWM và cầu H |
| `MotorBts_SetSpeed()` | Đặt cùng tốc độ cho cả 4 động cơ |
| `MotorBts_StopOne()` | Dừng một động cơ |
| `MotorBts_RunOne()` | Chạy một động cơ theo chiều và tốc độ chỉ định |
| `MotorBts_RunCommand()` | Nhận lệnh có dấu `-1000..1000` rồi đổi thành chiều và tốc độ |
| `MotorBts_DIR()` | Đổi trạng thái chiều cho cả 4 động cơ |
| `MotorBts_Run()` | Chạy cả 4 động cơ cùng chiều và tốc độ |

















