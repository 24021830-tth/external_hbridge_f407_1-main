# KIẾN TRÚC VI SAI ĐIỆN TỬ HỢP NHẤT DÒNG ĐIỆN VÀ IMU

> Trạng thái tài liệu: **DỰ THẢO KIẾN TRÚC TÍCH HỢP — CHƯA CẤP QUYỀN TÁC ĐỘNG MOTOR**  
> Phiên bản: `0.1-draft`  
> Ngày lập: `2026-09-03`  
> Dự án: `external_hbridge_f407` — STM32F407VGT6, FreeRTOS, xe vi sai 4 động cơ dùng BTS7960  
> Phạm vi: giải thích giá trị tạo ra khi kết hợp phản hồi dòng và IMU, kiến trúc điều khiển, thuật toán phối hợp và trạng thái xe tốt nhất có thể đạt

Tên file `diffinantial_electrical.md` được giữ đúng theo yêu cầu. Thuật ngữ kỹ thuật được dùng trong nội dung là **electronic differential / vi sai điện tử**.

---

## 0. Vai trò của tài liệu

Tài liệu này là lớp hiệp đồng giữa hai hợp đồng chi tiết:

1. `current.md`: acquisition dòng, hiệu chuẩn, current envelope, bảo vệ và current governor.
2. `docs/IMU.md`: acquisition IMU, hiệu chuẩn, estimator, yaw controller và fault policy IMU.

Hai tài liệu trên vẫn là nguồn ràng buộc chi tiết cho từng subsystem. Tài liệu này trả lời bốn câu hỏi tích hợp:

1. Dòng điện biết gì về xe?
2. IMU biết gì về xe?
3. Hai nguồn dữ liệu phối hợp thế nào mà không tranh quyền hoặc gây mất an toàn?
4. Nếu làm đúng và tuning tốt, xe đạt trạng thái điều khiển nào, cảm giác vận hành ra sao và vẫn còn giới hạn gì?

Các từ khóa:

- **PHẢI / KHÔNG ĐƯỢC**: yêu cầu cứng.
- **NÊN / KHÔNG NÊN**: phương án mặc định.
- **CÓ THỂ**: lựa chọn mở rộng.
- **TBD**: phải đo hoặc xác nhận trước khi bật quyền điều khiển.
- **Shadow**: tính toán và log nhưng không sửa lệnh motor.

Nguyên tắc trung tâm:

> **Dòng điện điều khiển biên effort/mô-men và bảo vệ; IMU điều khiển chuyển động quay của thân xe. Không dùng dòng làm yaw sensor và không dùng IMU làm current limiter.**

> **Kết hợp dòng + IMU tạo ra closed-loop yaw bên trong một current/torque envelope, nhưng vẫn chưa tạo ra closed-loop tốc độ dọc nếu không có encoder.**

---

## 1. Câu trả lời ngắn: kết hợp này tạo ra được gì?

### 1.1. Khi chỉ có điều khiển hiện tại

Lệnh trái/phải được chuyển thành duty cho hai bên. Ramp và state machine làm duty thay đổi có kiểm soát; current governor có thể giảm effort khi gần giới hạn.

Hệ thống biết:

- người lái đang yêu cầu gì;
- duty nào đã gửi tới motor;
- motor/cầu công suất đang chịu dòng bao nhiêu nếu current subsystem đã commissioning;
- state hiện tại là drive, coast, brake, reversal hay pivot.

Hệ thống chưa biết thân xe thực tế đang quay như thế nào.

### 1.2. Khi thêm IMU nhưng chưa kết hợp điều khiển

Hệ thống quan sát thêm:

- yaw-rate thực;
- relative yaw ngắn hạn;
- roll/pitch;
- acceleration, vibration và impact candidate;
- xe có phản ứng cùng dấu với lệnh quay hay không.

Nếu chỉ log IMU, xe chưa tự ổn định hơn; chỉ có thêm khả năng đo và chẩn đoán.

### 1.3. Khi kết hợp đúng dòng + IMU

Hệ thống có thể:

- đóng vòng yaw-rate khi đi thẳng, rẽ và pivot theo từng gate;
- tự giảm một bên để sửa lệch hướng nhưng không boost vượt lệnh;
- giảm toàn bộ effort khi một motor gần quá dòng mà vẫn hạn chế đổi curvature đột ngột;
- biết khi yaw controller đang hết quyền do current limit, saturation hoặc mặt đường;
- giảm acceleration/yaw authority khi current headroom thấp;
- phân biệt tốt hơn giữa tải lớn, yaw không đáp ứng, phản ứng sai chiều và lỗi sensor;
- chuyển về open-loop hoặc controlled stop có kiểm soát khi IMU/current invalid;
- đo định lượng độ mượt thay vì chỉ nhìn duty.

Kết quả tổng quát:

```text
Ý người lái
  + command shaping
  + yaw feedback từ IMU
  + effort/current constraint
  + state machine an toàn
  = chuyển động dễ dự đoán hơn trong giới hạn vật lý
```

### 1.4. Điều kết hợp này vẫn không tạo ra

Không có encoder, hệ thống vẫn không xác nhận được:

- tốc độ từng bánh;
- tốc độ dọc chính xác theo m/s;
- quãng đường/odometry đáng tin cậy;
- bánh nào đang trượt;
- slip ratio;
- xe đứng yên tuyệt đối;
- khả năng giữ tốc độ khi tải thay đổi;
- heading tuyệt đối lâu dài nếu chỉ tích phân gyro.

---

## 2. Mô hình đúng để hiểu sự phối hợp

### 2.1. Hai thành phần của lệnh vi sai

Với lệnh chuẩn hóa hai bên:

```text
u_left, u_right trong [-1, +1]
```

Tách thành:

```text
u_common = (u_right + u_left) / 2
u_diff   = (u_right - u_left) / 2
```

- `u_common`: thành phần làm xe tiến/lùi.
- `u_diff`: thành phần tạo yaw/rẽ.

Khôi phục:

```text
u_left  = u_common - u_diff
u_right = u_common + u_diff
```

App hiện đã gửi `left_permille/right_permille`, vì vậy phép trên chỉ dùng để phân tích ý định chuyển động hoặc tạo yaw reference. Không được coi input là `v/omega` rồi trộn thêm một lần làm sai lệnh.

### 2.2. Dòng điện nằm ở đâu trong mô hình?

Với motor DC trong miền phù hợp:

```text
torque_motor gần tỷ lệ với current_motor
```

Dòng giúp trả lời:

- motor đang tạo effort lớn hay nhỏ;
- motor/driver có gần biên an toàn không;
- lệnh tăng thêm còn bao nhiêu current headroom;
- tải có tăng bất thường không;
- có nghi kẹt hoặc đường công suất lỗi không.

Dòng không cho biết duy nhất:

- bánh đang quay nhanh bao nhiêu;
- thân xe đang quay bao nhiêu;
- cùng dòng có tạo cùng lực bám hay không.

Do đó **không được ép bốn motor có cùng dòng để giữ thẳng**. Bánh chịu tải khác nhau có thể cần dòng khác nhau dù xe đang đi đúng.

### 2.3. IMU nằm ở đâu trong mô hình?

Gyro Z hoặc yaw-rate đã chiếu theo phương thẳng đứng cho biết:

```text
r_measured = yaw-rate thực của thân xe
```

So với:

```text
r_reference = yaw-rate mong muốn từ mode/lệnh
```

ta có:

```text
e_yaw_rate = r_reference - r_measured
```

IMU vì vậy là cảm biến chính cho vòng yaw. Nó không biết motor nào đang quá dòng; current subsystem phải cung cấp constraint đó.

### 2.4. Quan hệ hiệp đồng

Hai subsystem không cộng raw measurement trực tiếp. Chúng gặp nhau ở **quyền điều khiển**:

- IMU yêu cầu một differential correction để giảm yaw error.
- Current governor quyết định effort còn được phép sử dụng.
- Safety arbiter quyết định correction có được áp hay phải dừng.
- Command allocator chiếu yêu cầu vào miền lệnh an toàn.

Có thể xem đây là bài toán:

```text
giảm yaw error nhiều nhất
trong khi:
    không vượt lệnh người lái
    không đổi dấu ngoài state machine
    không vượt current envelope
    không bỏ qua brake/timeout/fault
    không tạo bước duty/jerk nguy hiểm
```

---

## 3. Kiến trúc tổng thể được chọn

### 3.1. Sơ đồ dữ liệu và quyền điều khiển

```text
USART2 / AppControl
        |
        | left/right/brake + timestamp + validity
        v
Command Validation / Safety Gate
        |
        v
Base Left/Right Command
        |
        +----------------------------+
        |                            |
        v                            v
Motion Intent                  IMU Snapshot
(common/diff/mode)             gyro/accel/attitude/quality
        |                            |
        +------------+---------------+
                     v
              Yaw Controller
              r_ref - r_measured
                     |
                     v
       bounded reduction-only correction
                     |
                     v
       Ramp / Reversal / Pivot State Machine
                     |
                     +-------------------------+
                     |                         |
                     v                         v
              shaped commands          Current Snapshot
                                               |
                     +-------------------------+
                     v
       Current Envelope Governor / Safety
       common-scale + hard-fault decision
                     |
                     v
          motor calibration reduction-only
                     |
                     v
              MotorBts PWM / INH
                     |
                     v
                   XE
                     |
          +----------+-----------+
          |                      |
          v                      v
     current feedback       IMU feedback
```

### 3.2. Thứ tự xử lý bắt buộc

Trong kiến trúc hiện tại:

```text
App left/right
 -> DifferentialControl mapping
 -> yaw correction từ IMU
 -> ramp/reversal/pivot
 -> current common-scale
 -> motor calibration reduction-only
 -> PWM/INH
```

Lý do:

- yaw correction phải đi qua logic đảo chiều và pivot;
- current constraint phải áp lên lệnh cuối đang chuẩn bị ra actuator;
- calibration không được boost ngược lệnh sau limiter;
- emergency/hard-current có quyền cắt ngay;
- chỉ một module được ghi PWM/INH.

### 3.3. Hai đường tác động của current

Current nên có hai tác động khác nhau:

1. **Đường protection/governor phía trong**  
   Giảm `common_scale`, controlled stop hoặc emergency stop khi vượt envelope.

2. **Đường headroom advisory phía ngoài**  
   Cho command shaper/yaw controller biết còn bao nhiêu margin để chỉ giảm acceleration, jerk hoặc correction authority trước khi chạm limiter.

Đường advisory không được thay protection. Nếu advisory lỗi, protection vẫn phải hoạt động độc lập.

### 3.4. Snapshot hợp nhất

Một control update nên đọc một frame quan sát nhất quán:

```c
typedef struct
{
    AppControl_Snapshot_t app;
    CurrentSense_ControlFrame_t current;
    ImuControlFrame_t imu;
    MotorBts_AppliedState_t motor[4];
    uint32_t control_timestamp_us;
} VehicleObservationFrame_t;
```

Không yêu cầu current và IMU có cùng generation, nhưng từng snapshot phải:

- có timestamp;
- nằm trong freshness budget riêng;
- có quality hợp lệ cho mục đích đang dùng;
- được copy nguyên tử;
- không bị controller tái tích phân nhiều lần khi generation không đổi.

---

## 4. Thuật toán phối hợp baseline

### 4.1. Bước 1 — Xác nhận lệnh và mode

Nếu app invalid, disconnect, stale, timeout hoặc brake:

- yaw correction bằng 0;
- integral freeze/reset theo policy;
- đi vào đường dừng hiện tại;
- không dùng IMU để giữ hướng chống lại yêu cầu dừng.

Từ lệnh hợp lệ, xác định:

- `STOP`;
- `STRAIGHT_FORWARD`;
- `STRAIGHT_REVERSE`;
- `TURN_FORWARD`;
- `TURN_REVERSE`;
- `PIVOT_LEFT`;
- `PIVOT_RIGHT`;
- `REVERSAL_TRANSITION`;
- `PIVOT_TRANSITION`.

Mode phải có deadband và hysteresis để không rung giữa straight/turn.

### 4.2. Bước 2 — Tạo yaw-rate reference

Đi thẳng:

```text
r_ref = 0
```

Rẽ:

```text
r_ref = F(u_common, u_diff, direction, battery/load region)
```

Pivot:

```text
r_ref = F_pivot(command_magnitude, pivot_direction)
```

`F` phải đến từ phép đo trên đúng xe. Không được coi duty là tốc độ bánh hoặc dùng hằng số động học danh nghĩa chưa hiệu chuẩn làm ground truth.

Nếu chưa có map rẽ/pivot:

- straight assist có thể active sau gate;
- turn/pivot chỉ shadow;
- có thể dùng yaw damping giới hạn để giảm dao động nhưng không tuyên bố tracking yaw-rate chính xác.

### 4.3. Bước 3 — Yaw controller

Baseline:

```text
e_r = r_ref - r_measured

P = Kp(mode) * e_r
I = I + Ki(mode) * e_r * dt

delta_raw = P + I
delta_limit = clamp(delta_raw, -delta_max(mode), +delta_max(mode))
delta = slew_limit(delta_previous,
                   delta_limit,
                   delta_rate_limit(mode),
                   dt)
```

Release đầu có thể dùng P-only. Chỉ thêm I sau khi đã chứng minh cần xóa sai lệch tĩnh.

Integral phải freeze/back-calculate khi:

- correction saturate;
- reduction-only projection hết quyền;
- current governor đang derate;
- IMU/current stale;
- không có generation mới;
- brake/reversal/pivot transition;
- controller đang shadow/off.

### 4.4. Bước 4 — Tạo ứng viên trái/phải

```text
left_candidate  = left_base  - delta
right_candidate = right_base + delta
```

Yaw dương quy ước quay trái. Dấu phải được test riêng ở tiến, lùi và pivot.

### 4.5. Bước 5 — Projection reduction-only

Release an toàn đầu tiên không cho IMU boost hoặc reverse:

```text
project(base, candidate):
    if base == 0:
        return 0

    magnitude = clamp(sign(base) * candidate, 0, abs(base))
    return sign(base) * magnitude
```

Do đó:

```text
abs(left_corrected)  <= abs(left_base)
abs(right_corrected) <= abs(right_base)
sign(left_corrected) không đổi
sign(right_corrected) không đổi
```

Ưu điểm:

- không tạo chuyển động ngoài lệnh;
- không bỏ qua reversal state machine;
- correction failure khó tạo boost bất ngờ;
- phù hợp nguyên tắc giảm một chiều của project.

Đổi lại:

- sửa hướng bằng cách giảm một bên nên vận tốc dọc có thể giảm;
- không bù được mọi understeer/low-grip;
- controller phải báo `YAW_AUTHORITY_LIMITED` khi projection saturate.

### 4.6. Bước 6 — Ramp/reversal/pivot

Target đã correction vẫn phải đi qua:

- acceleration/deceleration slew;
- S-curve jerk limit nếu được triển khai;
- ramp về zero;
- brake wait;
- reversal protection;
- pivot transition đồng bộ bốn motor;
- pivot max 35% hiện tại.

IMU không được đưa correction trực tiếp sau state machine nếu correction đó có thể đổi dấu hoặc tạo duty step.

### 4.7. Bước 7 — Current envelope

Mỗi motor có soft/hard limit đã hiệu chuẩn:

```text
g_i = current_availability(i), 0 <= g_i <= 1
g_common = min(g_1, g_2, g_3, g_4)
```

Sau đó:

```text
left_governed  = g_common * left_shaped
right_governed = g_common * right_shaped
```

Common scaling:

- giảm effort toàn xe;
- không làm một bên bị cắt riêng đột ngột;
- giữ tương đối curvature sau yaw correction;
- bảo thủ khi chỉ một motor chạm limit;
- cho phép current subsystem giữ quyền bảo vệ độc lập.

Nếu hard limit/fault đạt điều kiện deglitch:

```text
MotorBts_EmergencyDisableAll()
```

Yaw controller không có quyền phản đối hoặc bù lại.

### 4.8. Bước 8 — Feedback saturation về yaw controller

Yaw controller phải nhận tối thiểu:

- correction requested;
- correction sau projection;
- current `common_scale`;
- soft-limit mask;
- actuator mode;
- output applied hoặc giới hạn gần đúng;
- reason vì sao authority bị giảm.

Nếu không phản hồi saturation, integral có thể tăng mãi khi current governor đang giảm toàn xe, rồi gây overshoot lúc current phục hồi.

---

## 5. Thuật toán nâng cao có thể thêm

### 5.1. Current-headroom adaptive shaping

Định nghĩa headroom chuẩn hóa cho motor hợp lệ:

```text
h_i = clamp((I_soft_i - I_control_i) / I_soft_i, 0, 1)
h_common = min(h_1, h_2, h_3, h_4)
```

Có thể dùng `h_common` để giảm trước:

- positive acceleration limit;
- jerk limit;
- yaw correction slew;
- pivot acceleration.

Ví dụ khái niệm:

```text
a_allowed = a_min + H(h_common) * (a_nominal - a_min)
```

Trong đó `H` có hysteresis và lọc. Deceleration cần giảm tải không được bị headroom logic ngăn cản.

Chức năng này chỉ có quyền giảm, không thay hard/soft limiter.

### 5.2. Heading hold ngắn hạn

Khi straight mode ổn định:

```text
psi_ref = relative_yaw tại lúc vào mode
e_psi = wrap(psi_ref - relative_yaw)
r_ref = clamp(K_heading * e_psi, -r_hold_max, +r_hold_max)
```

Vòng yaw-rate phía trong theo dõi `r_ref`.

Heading hold phải hủy khi:

- người lái yêu cầu rẽ;
- stop/brake;
- reversal/pivot;
- IMU resync/stale;
- gyro bias/calibration invalid.

Không gọi đây là giữ hướng tuyệt đối dài hạn.

### 5.3. Constraint-aware yaw allocator

Mức nâng cao có thể giải một bài toán phân bổ:

```text
minimize:
    W_yaw * yaw_error^2
  + W_effort * loss_of_common_effort^2
  + W_smooth * command_change^2

subject to:
    sign invariants
    no command above approved envelope
    current limits per motor
    correction slew/jerk limits
    pivot/reversal state constraints
```

Tuy nhiên current không phải phép đo mô-men bánh chính xác và không có wheel speed. Vì vậy allocator phức tạp chỉ được xem là experimental khi chưa có encoder. Baseline common-scale + reduction-only yaw correction dễ chứng minh an toàn hơn.

### 5.4. Chẩn đoán hợp nhất

Các rule có thể dùng:

| Dòng | IMU/chuyển động | Lệnh | Nhãn được phép |
|---|---|---|---|
| Cao | Yaw/accel thấp | Cao | `STALL_OR_HIGH_LOAD_SUSPECTED` |
| Bình thường | Yaw ngược dấu | Có rẽ | `YAW_DIRECTION_MISMATCH` |
| Một motor bất thường | Yaw vẫn đúng | Bình thường | `CURRENT_CHANNEL_OR_LOCAL_LOAD_SUSPECTED` |
| Dòng derate | Yaw error tăng | Có rẽ | `YAW_AUTHORITY_LIMITED_BY_CURRENT` |
| Thấp/0 | IMU chuyển động mạnh | Lệnh 0 | `EXTERNAL_MOTION_OR_IMPACT` |
| Cao dao động | Vibration cao | Duty theo vùng | `DRIVETRAIN_VIBRATION_SUSPECTED` |
| Current invalid | IMU valid | Bất kỳ | current policy quyết định stop/derate |
| Current valid | IMU invalid | Bất kỳ | yaw correction về 0/fallback |

Không có encoder thì không nâng `SUSPECTED` thành `CONFIRMED` cho stall/slip riêng từng bánh.

### 5.5. Tilt-aware envelope

Sau khi roll/pitch estimator và geometry vượt gate riêng, có thể:

- giảm acceleration khi dốc/nghiêng gần biên;
- giảm pivot authority trên mặt nghiêng;
- cảnh báo trước controlled stop;
- chọn coast/brake theo hazard analysis.

Không được tăng duty để “bù dốc” chỉ từ accelerometer. Việc giữ tốc độ lên dốc cần encoder hoặc nguồn vận tốc khác.

---

## 6. State machine hợp nhất

Một state enum duy nhất chứa mọi tổ hợp sẽ phình lớn và khó kiểm thử. Nên mô hình hóa bốn state machine trực giao.

### 6.1. Safety state

```text
BOOT_SAFE
READY
RUN
CONTROLLED_STOP
EMERGENCY_STOP
FAULT_LATCHED
```

| State | Motor authority |
|---|---|
| `BOOT_SAFE` | PWM 0, INH safe |
| `READY` | đủ điều kiện nhưng chưa có drive command |
| `RUN` | cho phép lệnh trong envelope |
| `CONTROLLED_STOP` | chỉ được giảm về 0 theo policy |
| `EMERGENCY_STOP` | cắt actuator ngay |
| `FAULT_LATCHED` | không tự chạy lại |

### 6.2. Motion state

```text
STOP
STRAIGHT_FORWARD
STRAIGHT_REVERSE
TURN_FORWARD
TURN_REVERSE
PIVOT_LEFT
PIVOT_RIGHT
RAMP_TO_ZERO
BRAKE_WAIT
```

Motion state đến từ lệnh base và state machine, không đến từ IMU suy đoán.

### 6.3. Feedback state

```text
FEEDBACK_OFF
CURRENT_SHADOW
IMU_SHADOW
ALL_SHADOW
CURRENT_ACTIVE_IMU_SHADOW
CURRENT_AND_IMU_ACTIVE
CURRENT_DEGRADED
IMU_DEGRADED
ALL_DEGRADED
```

Không được chỉ có boolean `feedback_ok`; cần biết subsystem nào invalid để chọn fallback đúng.

### 6.4. Authority state

```text
AUTHORITY_FULL_WITHIN_COMMAND
YAW_CORRECTION_LIMITED
CURRENT_DERATED
YAW_AND_CURRENT_LIMITED
NO_YAW_AUTHORITY
NO_DRIVE_AUTHORITY
```

State này giải thích vì sao yaw error còn tồn tại dù controller đang active.

### 6.5. Trạng thái tổng hợp publish ra telemetry

```c
typedef struct
{
    VehicleSafetyState_t safety;
    VehicleMotionState_t motion;
    VehicleFeedbackState_t feedback;
    VehicleAuthorityState_t authority;

    uint32_t active_faults;
    uint32_t latched_faults;

    uint16_t current_scale_permille;
    int16_t yaw_correction_permille;
    uint8_t current_valid_mask;
    uint8_t imu_valid;
} VehicleStateSnapshot_t;
```

Snapshot này là cách trả lời máy-readable cho câu hỏi “xe đang ở trạng thái nào”.

---

## 7. Priority và quyền quyết định

Từ cao xuống thấp:

1. Hardware emergency input hoặc hard-current fault đã xác nhận.
2. App invalid/disconnect/timeout/brake.
3. Safety arbiter controlled stop hoặc fault latch.
4. Reversal/pivot transition và invariant actuator.
5. Current validity/protection envelope.
6. IMU validity và yaw mode gate.
7. Command shaping/ramp.
8. Yaw correction trong authority còn lại.
9. Current common derating trong data path trước motor calibration.
10. Motor calibration reduction-only.
11. PWM/INH actuator.

Giải thích thứ tự logic và data path:

- hard-current ở mục 1 luôn được xử lý như safety event dù phép nhân `common_scale` xảy ra sau yaw correction trong data flow;
- yaw controller không được biến một yêu cầu stop thành turn;
- current subsystem không tự tạo yaw để cân bằng dòng;
- IMU subsystem không tự tạo common effort để giữ tốc độ;
- MotorBts không tự quyết định motion intent.

---

## 8. Hành vi mong muốn trong từng tình huống

### 8.1. Khởi động

Trạng thái tốt:

```text
safety   = BOOT_SAFE
motion   = STOP
feedback = shadow/off tùy subsystem
authority = NO_DRIVE_AUTHORITY
PWM = 0, INH safe
```

Current và IMU tự init, self-check, publish validity. Không subsystem nào tự làm motor quay để “test nhanh” khi boot.

### 8.2. Bắt đầu đi thẳng

Hành vi tốt:

- command tăng qua ramp/S-curve;
- current headroom làm acceleration giảm mềm nếu tải lớn;
- yaw correction bắt đầu từ 0 bằng bumpless transfer;
- gyro phát hiện lệch và chỉ giảm nhẹ bên gây lệch;
- không có step duty do enable feedback;
- không nghe/nhìn thấy hunting trái–phải.

### 8.3. Đi thẳng trên mặt đường tương đối đều

Trạng thái tốt nhất:

```text
r_ref gần 0
r_measured dao động nhỏ quanh 0
yaw correction nhỏ, không bias một phía kéo dài bất thường
current scale gần 1 nếu đủ headroom
không saturation, không fault
```

Xe giữ thẳng theo yaw ngắn hạn tốt hơn trim cố định và ít phụ thuộc pin/tải.

### 8.4. Hai bên mặt đường khác nhau

Hành vi tốt:

- IMU phát hiện yaw thực thay vì đoán từ duty/current;
- yaw controller tạo correction trong biên reduction-only;
- nếu correction đủ quyền, xe giảm lệch;
- nếu thiếu bám hoặc current limit làm hết quyền, state chuyển `YAW_AUTHORITY_LIMITED`;
- controller không tích phân vô hạn;
- xe có thể chậm lại để giữ an toàn thay vì tự boost một phía.

Không được cam kết luôn giữ đường thẳng trên mọi chênh lệch ma sát. Khi vật lý không còn authority, trạng thái đúng là báo giới hạn hoặc dừng có kiểm soát.

### 8.5. Rẽ bình thường

Hành vi tốt sau khi có yaw map:

- ý định rẽ vẫn đến từ chênh lệnh trái/phải;
- yaw controller chỉ sửa sai lệch giữa yaw-rate yêu cầu và thực;
- turn entry/exit không có correction step;
- current common-scale làm xe chậm lại khi cần nhưng hạn chế thay đổi curvature;
- correction và integral giảm khi limiter saturate;
- trái/phải có response tương xứng sau tuning riêng.

### 8.6. Pivot

Hành vi tốt:

- cả bốn motor về 0 và brake theo state machine trước khi vào/đổi pivot;
- correction bằng 0 trong transition;
- vào `PIVOT_DRIVE` mới cho yaw feedback;
- pivot duty không vượt 35% hiện tại;
- current envelope tính đến tyre scrub;
- yaw-rate không vọt mạnh và hai hướng pivot có cảm giác gần tương đương;
- khi người lái nhả, xe thoát pivot qua zero/brake, không giật đổi sang tiến.

### 8.7. Gặp vật cản hoặc tải tăng

Hành vi tốt:

- current tăng được phát hiện trước khi quá nhiệt/hư hỏng;
- positive acceleration và correction authority được giảm;
- common-scale giảm toàn xe;
- IMU cho biết thân xe còn phản ứng hay không;
- nếu kéo dài: `STALL_OR_HIGH_LOAD_SUSPECTED` rồi controlled stop theo policy;
- nếu hard current: emergency stop;
- không tự tăng ga hoặc tự đảo chiều thoát kẹt.

### 8.8. Một motor có current bất thường

Hành vi tốt:

- không ép ba motor còn lại lên cùng dòng;
- không tăng motor yếu/nghi kẹt;
- common-scale bảo thủ để giữ toàn hệ thống trong envelope;
- IMU theo dõi yaw consequence;
- fault context ghi đúng motor/channel/direction;
- hard fault mặc định dừng toàn xe như hợp đồng hiện tại.

### 8.9. IMU mất dữ liệu khi đang chạy

Hành vi tốt:

- freeze integral ngay;
- correction ramp về 0 theo fallback slew;
- không có cú giật khi mất assist;
- current/ramp/reversal path vẫn hoạt động;
- tiếp tục open-loop hay controlled stop theo policy của motion mode;
- không tự re-enable giữa maneuver cho đến khi đủ sample/re-arm.

### 8.10. Current data mất hoặc invalid

Hành vi tốt:

- IMU không được coi là thay thế current protection;
- current governor thực hiện controlled stop/emergency theo thời gian và fault class;
- yaw integral freeze;
- correction không kéo dài stop;
- fault latch nếu policy yêu cầu.

### 8.11. App timeout hoặc brake

Hành vi tốt:

- mọi yaw/heading request bị hủy;
- heading hold không chống lại stop;
- xe dừng theo priority hiện tại;
- hard safety có thể bỏ qua jerk nếu cần;
- IMU/current tiếp tục log fault context nếu còn hoạt động.

---

## 9. Xe đạt trạng thái nào nếu làm tốt nhất với dòng + IMU?

### 9.1. Trạng thái điều khiển cực đại thực tế

Nếu toàn bộ acquisition, hiệu chuẩn, estimator, current envelope và tuning đạt, cấu hình **dòng + IMU nhưng chưa có encoder** có thể được mô tả chính xác là:

> **Xe vi sai có closed-loop yaw-rate/heading tương đối ngắn hạn, command shaping và closed-loop current/effort limiting; longitudinal speed vẫn open-loop.**

Đây là trạng thái rất hữu ích và tốt hơn đáng kể so với duty-only, nhưng không phải full motion control.

### 9.2. Ma trận năng lực

| Đại lượng/chức năng | Đo trực tiếp | Được điều khiển kín | Được constraint | Chỉ suy đoán | Chưa có |
|---|---:|---:|---:|---:|---:|
| Lệnh/duty motor | Biết từ firmware, không phải tốc độ thực | Không | Có |  |  |
| Dòng từng motor | Có | Envelope phản hồi, không tracking current setpoint | Có |  |  |
| Effort/mô-men gần đúng |  |  | Có qua dòng | Có |  |
| Yaw-rate thân xe | Có qua gyro | Có | Có giới hạn correction |  |  |
| Relative heading ngắn hạn | Không; ước lượng bằng tích phân gyro | Có thể hold | Có | Có drift |  |
| Roll/pitch | Không; ước lượng từ gyro + accel | Không baseline | Có thể cảnh báo |  |  |
| Acceleration/vibration | Accel có; vibration là metric suy ra | Không baseline | Có thể derate |  |  |
| Vận tốc từng bánh |  |  |  |  | Thiếu encoder |
| Vận tốc dọc chính xác |  |  |  | IMU rất ngắn hạn, không tin cậy | Thiếu encoder/reference |
| Odometry/vị trí |  |  |  |  | Thiếu encoder/localization |
| Slip ratio/bánh trượt |  |  |  | Chỉ suspected | Thiếu wheel speed + vehicle speed |
| Stall từng bánh |  |  |  | Chỉ suspected | Thiếu wheel speed |
| Heading tuyệt đối lâu dài |  |  |  | Gyro drift | Thiếu reference đã kiểm chứng |

### 9.3. Cảm giác vận hành tốt nhất có thể đạt

Người lái nên cảm nhận:

- ga lên/xuống đều và ít giật hơn nếu S-curve/current-adaptive shaping được hoàn thiện;
- đi thẳng ít phải sửa tay hơn;
- cùng một lệnh rẽ cho phản ứng gần lặp lại hơn;
- xe không giật đổi hướng khi một motor chạm current soft limit;
- pivot vào/ra có trật tự và tốc độ quay ít vọt;
- khi tải quá lớn, xe chủ động chậm lại hoặc dừng thay vì cố kéo;
- khi mất IMU, xe không đá lái mà trả dần về hành vi open-loop;
- fault có trạng thái rõ ràng thay vì motor hành xử khó đoán.

### 9.4. Telemetry tốt nhất phải thể hiện

Ở trạng thái khỏe mạnh:

```text
safety = RUN
motion = STRAIGHT/TURN/PIVOT đúng ý người lái
feedback = CURRENT_AND_IMU_ACTIVE
authority = FULL hoặc lý do LIMITED rõ ràng
app_fresh = true
current_valid_mask = 0x0F
imu_valid = true
current_scale gần 1000 permille khi không tải nặng
yaw correction nhỏ khi xe cân bằng
no active/latched fault
deadlines met
```

“Tốt nhất” không có nghĩa correction luôn bằng 0. Correction nhỏ, thay đổi chậm và có tương quan đúng với disturbance mới là dấu hiệu vòng yaw đang làm việc đúng.

### 9.5. Giới hạn vật lý vẫn còn

Dù tuning hoàn hảo:

- reduction-only correction có thể chỉ giữ hướng bằng cách làm xe chậm;
- mặt đường quá trơn làm lệnh motor không tạo đủ yaw moment;
- current limit quá thấp làm yaw controller hết authority;
- gyro bias làm heading dài hạn drift;
- skid-steer có tyre scrub và plant thay đổi mạnh theo tải/bề mặt;
- không encoder thì không giữ được tốc độ dọc chính xác;
- một sensor tốt không sửa được mapping dây, chassis cong, lốp hỏng hoặc nguồn yếu.

Controller tốt phải **nhận biết và công bố hết quyền**, không che giới hạn bằng integral hoặc boost nguy hiểm.

---

## 10. Các cấp trưởng thành của hệ thống

### Level 0 — Duty-only

- lệnh trái/phải;
- ramp/reversal/pivot;
- không có quan sát dòng/yaw có quyền tác động.

Trạng thái: điều khiển hở vòng hoàn toàn về động lực học.

### Level 1 — Current shadow + IMU shadow

- đo dòng và chuyển động;
- chưa sửa duty;
- xây baseline, mapping, calibration và telemetry.

Trạng thái: quan sát tốt, chưa tự ổn định.

### Level 2 — Current protection/governor active

- giới hạn effort và hard fault;
- common scaling bảo toàn tương đối tỷ lệ trái/phải;
- yaw vẫn open-loop.

Trạng thái: an toàn tải tốt hơn nhưng vẫn có thể lệch hướng theo mặt đường.

### Level 3 — Straight yaw assist

- current active;
- IMU yaw-rate active khi đi thẳng;
- reduction-only correction;
- turn/pivot còn shadow.

Trạng thái: mức lợi ích/rủi ro tốt nhất để đưa vào vận hành đầu tiên.

### Level 4 — Turn/pivot yaw assist

- yaw maps đã đo;
- gain schedule theo mode;
- anti-windup nhận current saturation;
- heading hold ngắn hạn tùy chọn;
- fault fallback đã nghiệm thu.

Trạng thái: mức tối đa thực tế được khuyến nghị cho cấu hình current + IMU không encoder.

### Level 5 — Constraint-aware allocator thử nghiệm

- phân bổ correction theo current headroom và yaw objective;
- optimization/allocator có invariant safety;
- vẫn không có wheel-speed feedback.

Trạng thái: có thể hiệu quả hơn nhưng chưa đủ bằng chứng để mặc định tốt hơn Level 4.

### Level 6 — Encoder + current + IMU

- speed PI từng bên/bánh;
- yaw/heading outer loop;
- current torque envelope;
- odometry và traction observer tốt hơn.

Trạng thái: mới tiến gần full closed-loop motion control.

---

## 11. Kiến trúc tốt nhất khi sau này có encoder

```text
App/path command
    |
    v
S-curve motion shaper: v_ref, r_ref
    |
    v
Yaw/heading controller từ IMU
    |
    v
Left/right speed references
    |
    v
Wheel/side speed PI từ encoder
    |
    v
Constraint-aware torque/current allocation
    |
    v
Current envelope + protection
    |
    v
MotorBts actuator
```

Khi đó ba lớp phản hồi có nhiệm vụ riêng:

| Lớp | Feedback | Mục tiêu |
|---|---|---|
| Motion/yaw outer loop | IMU + odometry | hướng và quỹ đạo |
| Speed loop | encoder | tốc độ từng bên/bánh |
| Torque/protection inner constraint | current | mô-men gần đúng và an toàn điện/nhiệt |

Đây là cấu trúc đầy đủ hơn vì:

- encoder giữ tốc độ;
- IMU sửa yaw/slip tổng thể;
- current bảo vệ và giới hạn mô-men;
- mỗi sensor được dùng cho đại lượng nó thực sự quan sát được.

---

## 12. Anti-pattern bắt buộc tránh

### 12.1. Cân bằng dòng để giữ thẳng

Sai:

```text
I_left > I_right -> tăng duty_right hoặc giảm duty_left để ép I bằng nhau
```

Lý do: tải bánh khác nhau không đồng nghĩa yaw sai; có thể làm xe tự lái lệch hoặc boost motor lỗi.

Đúng: dùng IMU đo yaw error; dùng current làm constraint.

### 12.2. IMU correction sau mọi safety layer

Sai: cộng correction trực tiếp vào CCR sau current governor/reversal state machine.

Hậu quả: có thể vượt current envelope, đổi dấu hoặc tạo step duty.

### 12.3. Current governor từng motor không phối hợp

Sai: một motor chạm soft limit thì cắt riêng motor đó ngay.

Hậu quả: xe skid-steer có thể yaw mạnh và motor còn lại cùng bên chịu tải tăng.

Baseline đúng: common-scale; allocator khác chỉ sau phân tích/test riêng.

### 12.4. Integral không biết saturation

Sai: yaw PI tiếp tục tích phân khi current scale thấp hoặc reduction-only hết quyền.

Hậu quả: correction vọt khi limiter nhả.

### 12.5. Dùng accelerometer làm speed sensor

Sai: tích phân acceleration lâu dài để đóng vòng tốc độ.

Hậu quả: gravity leakage, bias và vibration làm speed drift.

### 12.6. Bật feedback chỉ vì sensor có dữ liệu

Sai: WHO_AM_I đúng rồi active controller.

Đúng: acquisition -> calibration -> estimator -> shadow -> low-authority active -> mở envelope theo gate.

### 12.7. Che trạng thái hết quyền

Sai: giữ mode `ACTIVE_OK` dù correction/current saturate kéo dài.

Đúng: publish `YAW_AUTHORITY_LIMITED`, `CURRENT_DERATED` hoặc `NO_DRIVE_AUTHORITY` kèm reason.

---

## 13. Tuning và trình tự đưa vào hoạt động

### 13.1. Thứ tự bắt buộc

1. Xác nhận mapping motor, chiều quay và state machine hiện tại.
2. Hoàn tất phần cứng/current calibration và current gates trong `current.md`.
3. Bật current acquisition shadow; xác minh không nhiễu sai theo PWM.
4. Bật current protection/governor theo đúng gate hiện có.
5. Hoàn tất IMU-G0..IMU-G3 trong `docs/IMU.md`.
6. Chạy IMU và yaw controller shadow, ghi full telemetry.
7. Xác nhận dấu correction ở tiến/lùi bằng test low speed.
8. Bật straight yaw P-only với correction limit nhỏ.
9. Tuning Kp, correction slew và mode hysteresis.
10. Chỉ thêm Ki khi có steady-state yaw error rõ.
11. Test current derating đồng thời yaw active; chốt anti-windup.
12. Đo/fill yaw map rẽ; mở turn assist.
13. Tuning pivot riêng; không copy gain straight.
14. Chỉ sau cùng mới xét heading hold, tilt envelope hoặc allocator nâng cao.

Không chỉnh current limit, yaw gain, ramp và calibration motor cùng lúc; sẽ không biết cải thiện/xấu đi đến từ đâu.

### 13.2. Dữ liệu cần cho tuning

- app left/right và mode;
- base target;
- shaped target;
- yaw reference/measured/error;
- P/I terms;
- correction raw/limited/projected;
- current từng motor;
- current scale/masks;
- applied motor command/state;
- IMU/current age/generation/quality;
- acceleration, vibration, roll/pitch;
- battery voltage nếu có;
- ground truth ngoài khi đánh giá quỹ đạo/tốc độ.

### 13.3. Nguyên tắc tuning

- bắt đầu P-only;
- bandwidth yaw thấp hơn đáng kể bandwidth IMU sau filter;
- tổng delay càng lớn thì gain an toàn càng thấp;
- gain tiến/lùi/pivot có thể khác nhau;
- correction limit nhỏ trước, mở dần theo log;
- không dùng gain lớn để che mapping sai hoặc gyro bias;
- không dùng Ki để bù current saturation kéo dài;
- so sánh A/B trên cùng pin, tải, lốp và mặt đường.

---

## 14. Telemetry và chỉ số chứng minh xe tốt hơn

### 14.1. Không dùng nhận xét chủ quan làm tiêu chí duy nhất

“Xe có vẻ mượt” không đủ. Phải so baseline và integrated control bằng số liệu.

### 14.2. Metric đi thẳng

- yaw-rate RMS và peak với `r_ref = 0`;
- relative yaw sau đoạn chạy cố định thời gian/quãng đường tham chiếu;
- correction RMS/peak;
- thời gian correction saturation;
- số lần hunting đổi dấu;
- tốc độ dọc tham chiếu ngoài nếu có.

### 14.3. Metric rẽ/pivot

- yaw-rate tracking RMS;
- rise time;
- overshoot;
- settling time;
- left/right symmetry;
- yaw angle tại lúc nhả lệnh nếu có ground truth;
- current peak/RMS và tyre scrub indicator.

### 14.4. Metric độ mượt

- longitudinal/lateral acceleration RMS/peak;
- angular acceleration RMS/peak;
- jerk estimate RMS/peak;
- duty step lớn nhất;
- correction slew lớn nhất;
- số lần transition không bumpless.

### 14.5. Metric safety/robustness

- current soft/hard events;
- time in derate;
- false/nuisance stop;
- sensor stale/error count;
- missed deadline;
- stop time/distance;
- fault recovery time;
- stack/CPU load;
- telemetry drop count.

### 14.6. Tiêu chí trạng thái tối ưu

Một release tốt phải đồng thời:

- giảm yaw error có ý nghĩa;
- không tăng current peak nguy hiểm;
- không tăng jerk/oscillation;
- không tạo sign reversal ngoài lệnh;
- không boost vượt envelope đã phê duyệt;
- fallback không gây steering step;
- deadline và freshness đạt;
- trạng thái authority/fault phản ánh đúng thực tế.

Nếu yaw tốt hơn nhưng current/jerk/fault rate xấu đi, chưa được coi là tối ưu.

---

## 15. Ma trận test tích hợp

### 15.1. Host test

- common/differential decomposition và reconstruction;
- straight/turn/pivot mode hysteresis;
- yaw PI P/I/anti-windup;
- reduction-only projection mọi tổ hợp dấu;
- correction không tạo lệnh từ zero;
- correction không đổi dấu;
- current common-scale bảo toàn tỷ lệ sau correction;
- current derate feedback làm freeze integral;
- stale current/IMU/app priority;
- reversal/pivot transition tắt correction;
- timestamp wrap/generation duplicate;
- state machine safety/motion/feedback/authority;
- combined fault priority;
- replay log với current limit + yaw disturbance đồng thời.

### 15.2. Bench/HIL

- inject current theo từng motor và yaw trace mô phỏng;
- current soft limit trong khi yaw correction khác 0;
- hard current và xác nhận emergency thắng yaw;
- IMU stale trong straight/turn/pivot;
- current stale nhưng IMU tốt;
- cả hai stale;
- app timeout khi integral khác 0;
- PWM/current acquisition/IMU bus chạy đồng thời;
- UART load/telemetry queue đầy;
- reset giữa maneuver;
- CPU/stack/deadline soak.

### 15.3. Ground test

- thẳng tiến/lùi nhiều mức lệnh;
- tải lệch trái/phải;
- mặt đường hai bên khác nhau;
- pin đầy/yếu;
- rẽ trái/phải cùng profile;
- pivot trái/phải;
- current derating có kiểm soát;
- vật cản có giới hạn cơ khí/test rig;
- mất IMU bằng test hook;
- command timeout/brake;
- dốc/nghiêng nếu thuộc operating envelope.

Mỗi test active phải có baseline shadow/open-loop tương ứng.

---

## 16. Gate tích hợp đề xuất

Các gate này không thay thế gate riêng trong `current.md` và `docs/IMU.md`.

### DE-G0 — Contract alignment

- mapping, dấu, đơn vị và priority thống nhất;
- current/IMU parameter versions xác định;
- fallback policy theo mode;
- telemetry schema hợp nhất;
- tiêu chí định lượng được duyệt.

### DE-G1 — Dual shadow

- current và IMU cùng chạy shadow;
- timestamp/freshness hoạt động;
- không mất deadline;
- replay dựng lại được maneuver;
- predicted correction đúng dấu.

### DE-G2 — Current active, yaw shadow

- current governor/protection đã nghiệm thu;
- yaw controller quan sát chính xác derating/saturation;
- anti-windup shadow đúng;
- không có conflict owner.

### DE-G3 — Straight assist

- reduction-only;
- P-only hoặc PI đã giới hạn;
- low speed/envelope nhỏ;
- yaw metric tốt hơn baseline;
- current/jerk/fault metric không xấu không chấp nhận được.

### DE-G4 — Turn/pivot assist

- yaw maps và gain schedule đã đo;
- transition bumpless;
- symmetry đạt target;
- current-limited maneuver có state đúng;
- no boost/no reverse invariant pass.

### DE-G5 — Integrated release

- full fault injection;
- ground soak;
- parameter/calibration/version locked;
- operator guide và rollback;
- telemetry/fault codes tài liệu hóa;
- trạng thái mặc định sau reset rõ ràng.

---

## 17. File/module dự kiến bị ảnh hưởng khi triển khai

| File/module | Thay đổi tích hợp dự kiến |
|---|---|
| `Core/Inc/vehicle_control.h` | thêm current + IMU observation/control frame |
| `Core/Src/vehicle_control.c` | mode gating, yaw apply, saturation feedback và priority |
| `Core/Inc/differential_control.h` | kiểu dữ liệu common/diff hoặc helper nếu cần |
| `Core/Src/differential_control.c` | chỉ phân tích/mapping; không đọc sensor |
| `Core/Inc/yaw_controller.h` | input gồm motion intent, IMU và authority feedback |
| `Core/Src/yaw_controller.c` | PI/projection/anti-windup/state result |
| `Core/Inc/current_governor.h` | expose result/headroom/saturation semantic nếu cần |
| `Core/Src/current_governor.c` | giữ common-scale/protection; không điều khiển yaw trực tiếp |
| `Core/Inc/vehicle_state.h` | snapshot safety/motion/feedback/authority đề xuất |
| `Core/Src/vehicle_state.c` | tổng hợp trạng thái và reason codes |
| `Core/Src/freertos.c` | snapshot acquisition/control scheduling |
| `Core/Src/usart6_log.c` | telemetry hợp nhất và decimation |
| `tests/test_yaw_controller.c` | PI/projection/anti-windup |
| `tests/test_vehicle_control.c` | full integration và priority |
| `tests/test_vehicle_state.c` | state transitions/reason codes |

Không nhất thiết phải tạo toàn bộ module ngay. Phải giữ owner rõ ràng và tránh để `vehicle_control.c` trở thành nơi chứa raw driver, estimator, current conversion và log cùng lúc.

---

## 18. Pseudocode tích hợp chuẩn

```text
VehicleControl_Update(observation):
    validate app snapshot

    if emergency or hard_current_fault:
        reset/freeze yaw
        emergency_disable_all()
        publish EMERGENCY_STOP
        return

    if app invalid, timeout or brake:
        reset/freeze yaw
        execute existing stop path
        publish CONTROLLED_STOP or STOP
        return

    base = DifferentialControl_Compute(app.left, app.right)
    motion = classify_motion(base, hysteresis)

    current_ok = validate_current_for_required_motors()
    imu_ok = validate_imu_for_selected_yaw_mode()

    if current policy requires stop:
        freeze yaw
        controlled_or_emergency_stop()
        publish current fault state
        return

    yaw_result = YawController_Update(
        base,
        motion,
        imu_snapshot,
        previous_current_scale,
        previous_actuator_saturation,
        new_sample_dt)

    if transition active:
        reset_or_freeze_yaw_as_required()
        corrected = base
    else if yaw not authorized or not imu_ok:
        decaying_delta = slew_previous_correction_toward_zero()
        corrected = reduction_only_project(
            base,
            mix_yaw_correction(base, decaying_delta))
    else:
        corrected = reduction_only_project(base, yaw_result.candidate)

    shaped = ExistingRampReversalPivot_Update(corrected)

    current_result = CurrentGovernor_Update(
        current_snapshot,
        shaped,
        actuator_state,
        dt)

    if current_result.hard_fault:
        emergency_disable_all()
        publish EMERGENCY_STOP
        return

    if current_result.controlled_stop:
        override target toward zero

    governed = common_scale(shaped, current_result.scale)
    calibrated = motor_calibration_reduction_only(governed)
    MotorBts_Apply(calibrated)

    feed saturation/current result to next yaw update
    publish unified vehicle state and telemetry
```

Runtime invariants:

```text
no IMU correction can create motion from zero
no IMU correction can reverse a side
no correction can bypass pivot/reversal state
no yaw integral update on duplicate/stale IMU data
no yaw boost can cancel current derating
hard current/app brake always wins
all applied outputs traceable to one observation frame
```

---

## 19. Biên bản quyết định kiến trúc

### 19.1. Các quyết định đã chọn

- [x] Dòng là effort/current constraint, không phải yaw reference.
- [x] IMU yaw-rate là feedback chính để sửa hướng.
- [x] App left/right vẫn là motion intent gốc; không trộn vi sai lần hai.
- [x] Yaw correction được áp trước ramp/reversal/pivot.
- [x] Current common-scale được áp sau shaping và trước motor calibration/PWM.
- [x] Hard-current, brake và timeout có priority cao hơn yaw.
- [x] Release đầu dùng reduction-only correction.
- [x] Không ép motor/bên có cùng dòng.
- [x] Không boost, không tự reverse, không auto-escape.
- [x] Anti-windup phải biết current/projection saturation.
- [x] Straight assist được bật trước turn/pivot.
- [x] Hết quyền phải được publish thành state, không che bằng integral.
- [x] Dòng + IMU không được gọi là closed-loop tốc độ dọc.

### 19.2. Các quyết định còn cần chủ xe xác nhận

- [ ] IMU part/bus/pin/orientation theo `docs/IMU.md`.
- [ ] Current hardware/mapping/limits theo `current.md`.
- [ ] Release đầu chỉ straight hay gồm turn/pivot.
- [ ] IMU mất trong từng mode: fallback open-loop hay controlled stop.
- [ ] Mức correction reduction-only tối đa.
- [ ] Operating surfaces/load/speed envelope.
- [ ] Định nghĩa định lượng của “đi thẳng”, “rẽ đúng” và “mượt”.
- [ ] Có bật heading hold ngắn hạn hay không.
- [ ] Có bật tilt/impact safety hay chỉ telemetry.
- [ ] Lộ trình encoder.

### 19.3. Trạng thái mục tiêu để ký nghiệm thu

Chỉ ký mức current + IMU tối đa khi:

```text
current mapping/calibration/protection = VERIFIED + ACTIVE
IMU identity/orientation/calibration = VERIFIED
IMU estimator = READY + FRESH
yaw controller = TESTED + MODE-GATED
correction = REDUCTION_ONLY + RATE_LIMITED
anti-windup = CURRENT/PROJECTION AWARE
reversal/pivot safety = PRESERVED
telemetry = RECONSTRUCTABLE
fault injection = PASSED
ground metrics = BETTER THAN BASELINE
speed/odometry claims = NOT MADE WITHOUT ENCODER
```

---

## 20. Kết luận

Kết hợp đúng dòng điện và IMU tạo ra một hệ vi sai điện tử có hai năng lực bổ sung nhau:

- **Dòng điện** giữ xe trong biên effort, tải và an toàn của motor/driver/dây/pin.
- **IMU** quan sát và sửa chuyển động yaw thật của thân xe.

Trạng thái tốt nhất thực tế khi chưa có encoder là:

> **Xe có current/effort envelope kín, yaw-rate kín, giữ hướng tương đối ngắn hạn, ramp/reversal/pivot an toàn và khả năng tự nhận biết khi hết quyền; tốc độ dọc vẫn là open-loop.**

Khi vận hành tốt, xe sẽ đi thẳng ít lệch hơn, rẽ/pivot nhất quán hơn, giảm công suất mềm khi quá tải, không đổi hướng đột ngột khi limiter tác động và fallback có kiểm soát khi một sensor lỗi.

Điều quan trọng không phải làm controller luôn cố đạt yaw bằng mọi giá. Hệ thống tốt nhất là hệ thống biết rõ ba trạng thái:

```text
đang điều khiển được
đang bị giới hạn nhưng còn an toàn
đã hết quyền và phải giảm/dừng
```

Muốn tiến từ trạng thái này sang full motion control — giữ tốc độ, odometry và traction — phải bổ sung encoder hoặc nguồn vận tốc độc lập, sau đó xây cascade encoder + IMU + current đúng vai trò của từng feedback.

---

## 21. Bản đồ chèn code khi chính thức dùng IMU + dòng để điều khiển vi sai

Mục này chỉ là **địa chỉ triển khai cho lần tích hợp sau**. Việc ghi mục này không có nghĩa IMU hoặc current governor đã được cấp quyền tác động motor.

### 21.1. Trạng thái code hiện tại

| Thành phần | Địa chỉ hiện tại | Trạng thái |
|---|---|---|
| Đọc và parse BNO080 | `Core/Inc/bno080.h`, `Core/Src/bno080.c` | Đã có module độc lập, chưa vào build firmware |
| Xử lý và publish IMU snapshot | `Core/Inc/imu.h`, `Core/Src/imu.c` | Đã có module độc lập, chỉ có trạng thái `*_SHADOW` |
| Đọc phản hồi dòng | `Core/Inc/current.h`, `Core/Src/current.c` | Đang chạy acquisition; `Current_GetSnapshot()` ở `Core/Src/current.c:519` |
| Chính sách giới hạn dòng | `Core/Inc/current_governor.h`, `Core/Src/current_governor.c` | File còn tồn tại nhưng đang offline, không nằm trong CMake và không tác động motor |
| Ánh xạ trái/phải sang M1..M4 | `Core/Inc/differential_control.h`, `Core/Src/differential_control.c` | Đang hoạt động, nên tiếp tục giữ stateless |
| Ramp/reversal/pivot và ghi motor | `Core/Inc/vehicle_control.h`, `Core/Src/vehicle_control.c` | Là nơi duy nhất nên hợp nhất yaw correction và current constraint |
| Vòng task điều khiển | `Core/Src/freertos.c:127`, hàm `StartDefaultTask()` | Hiện chỉ lấy app snapshot rồi gọi `VehicleControl_Update()` |

Hiện project **chưa cấu hình SPI cho BNO080** trong `external_hbridge_f407.ioc`. Vì bus, pin CS/INT/RST/WAKE và hướng lắp vẫn chưa được xác nhận nên không được tự gán cứng các giá trị này.

### 21.2. Các file mới nên tạo khi bắt đầu tích hợp

Tạo đúng hai lớp bổ sung sau:

```text
Core/Inc/imu_service.h
Core/Src/imu_service.c
    - sở hữu Bno080_t và Imu_t
    - cài các callback SPI/GPIO/time cho Bno080Transport_t
    - nhận notification từ DRDY
    - gọi Imu_Start(), Imu_ProcessDataReady()
    - chỉ publish/copy ImuSnapshot_t, không gọi motor

Core/Inc/yaw_controller.h
Core/Src/yaw_controller.c
    - tạo r_ref từ lệnh trái/phải và mode
    - tính error từ ImuSnapshot_t.vertical_yaw_rate_rad_s
    - P/PI, slew-limit, anti-windup và reduction-only projection
    - không gọi SPI, ADC, MotorBts_* hoặc Current_*
```

Không đưa thuật toán yaw stateful vào `Core/Src/differential_control.c`; file đó chỉ nên chuyển lệnh trái/phải thành lệnh M1..M4 và phân loại hướng.

### 21.3. Địa chỉ cấu hình phần cứng BNO080

1. Mở `external_hbridge_f407.ioc` và cấu hình:
   - một SPI master mode 3, MSB first, clock không quá 3 MHz;
   - chân `BNO080_CS`, `BNO080_RST`, `BNO080_WAKE` là GPIO output;
   - chân `BNO080_INT` là EXTI theo mức/cạnh phù hợp với wiring thực;
   - DMA SPI nếu chọn đường truyền DMA;
   - NVIC cho EXTI và DMA đã chọn.
2. Sinh lại code CubeMX. Các hàm `MX_SPIx_Init()` và file `Core/Src/spi.c`, `Core/Inc/spi.h` chỉ xuất hiện sau bước này.
3. Không sửa pin trực tiếp trong generated code trước khi cập nhật bảng pin/orientation ở `docs/IMU.md`.

Điểm gọi khởi tạo peripheral nằm trong `Core/Src/main.c:99-108`, cùng nhóm `MX_GPIO_Init()`, `MX_DMA_Init()`, `MX_ADC1_Init()`. Chèn `MX_SPIx_Init()` ở nhóm này nếu CubeMX chưa tự sinh đúng thứ tự.

### 21.4. Địa chỉ thêm source vào firmware

Trong `CMakeLists.txt:46`, block `target_sources(...)`, ngay sau `Core/Src/current.c`, thêm:

```cmake
Core/Src/bno080.c
Core/Src/imu.c
Core/Src/imu_service.c
Core/Src/yaw_controller.c
Core/Src/current_governor.c
```

Chỉ thêm `current_governor.c` khi mapping dòng, calibration và soft/hard limit đã được xác nhận. Nếu chỉ chạy shadow thì vẫn build module nhưng để `governor_enabled = 0` và không sửa lệnh motor.

### 21.5. Địa chỉ khởi tạo software

Trong `Core/Src/main.c`:

- tại `Core/Src/main.c:29-37`, block `USER CODE BEGIN Includes`, thêm `#include "imu_service.h"`;
- tại `Core/Src/main.c:109-117`, block `USER CODE BEGIN 2`, gọi `ImuService_Init()` sau khi GPIO/SPI/DMA đã init;
- chỉ tạo object, callback transport và cấu hình ở đây; **không gọi vòng đọc blocking vô hạn trong `main()`**;
- `VehicleControl_Init()` tại `Core/Src/main.c:115` vẫn là nơi khởi tạo state của yaw controller và current governor thông qua các hàm con tương ứng.

Thứ tự dự kiến:

```c
Usart6Log_Init();
MotorBts_InitSafe();
Current_Init();
ImuService_Init();       /* Bno080_Init + Imu_Init, chưa cấp quyền motor */
AppControl_Init();
VehicleControl_Init();   /* YawController_Init + CurrentGovernor_Init */
```

### 21.6. Địa chỉ tạo task IMU

Trong `Core/Src/freertos.c`:

1. Thêm `#include "imu_service.h"` tại block include `Core/Src/freertos.c:27-34`.
2. Khai báo `imuTaskHandle` và `imuTask_attributes` cạnh `currentTaskHandle` tại `Core/Src/freertos.c:63-69`. Priority khởi điểm nên là `osPriorityAboveNormal`; phải đo runtime trước khi thay đổi.
3. Tạo task bằng `osThreadNew(ImuService_Task, ...)` trong `MX_FREERTOS_Init()`, ngay sau chỗ tạo `currentTaskHandle` tại `Core/Src/freertos.c:110-112`.
4. Trong `ImuService_Task()` ở file mới `Core/Src/imu_service.c`:
   - gọi `Imu_Start()` một lần trong task context;
   - chờ notification từ `BNO080_INT`;
   - mỗi notification gọi `Imu_ProcessDataReady()` hoặc hoàn tất DMA rồi gọi `Imu_ProcessPacket()`;
   - tuyệt đối không gọi `VehicleControl_Update()` hoặc `MotorBts_*()` từ task này.

### 21.7. Địa chỉ nối ngắt DRDY/DMA

Trong `Core/Src/stm32f4xx_it.c`:

- thêm `#include "imu_service.h"` tại `Core/Src/stm32f4xx_it.c:24-28`;
- trong handler `EXTIx_IRQHandler` do CubeMX sinh cho chân `BNO080_INT`, chỉ clear cờ EXTI và gọi `ImuService_DataReadyFromISR()`;
- nếu dùng SPI DMA, handler DMA do CubeMX sinh chỉ xử lý HAL/LL DMA rồi đánh thức IMU task;
- **không parse packet, tính quaternion, chạy yaw PID hoặc gọi motor trong ISR**.

Tên chính xác của `EXTIx_IRQHandler`, SPI instance và DMA stream chỉ được chốt sau khi pin được xác nhận trong `external_hbridge_f407.ioc`; không được lấy lại SPI2/pin từ project BNO080 cũ một cách mặc định.

### 21.8. Địa chỉ đưa snapshot vào một control frame

Sửa `VehicleControl_Input_t` tại `Core/Inc/vehicle_control.h:17-20`:

```c
#include "current.h"
#include "imu.h"

typedef struct
{
    AppControl_Snapshot_t app;
    CurrentSense_Snapshot_t current;
    ImuSnapshot_t imu;
    uint32_t control_timestamp_us;
} VehicleControl_Input_t;
```

Sau đó sửa vòng lặp tại `Core/Src/freertos.c:133-141`, đọc mỗi snapshot đúng một lần trước `VehicleControl_Update()`:

```c
AppControl_Process();
(void)AppControl_GetSnapshot(&vehicle_input.app);
Current_GetSnapshot(&vehicle_input.current);
ImuService_GetSnapshot(now_us, &vehicle_input.imu);
vehicle_input.control_timestamp_us = now_us;
VehicleControl_Update(&vehicle_input);
```

`now_us` phải lấy từ cùng timebase monotonic mà IMU service dùng. Không dùng lại một IMU generation để tích phân yaw nhiều lần.

### 21.9. Địa chỉ chèn yaw correction

Trong `Core/Src/vehicle_control.c`:

1. Thêm include `yaw_controller.h` và `current_governor.h` ở đầu file.
2. Đặt `YawController_State_t` và `CurrentGovernor_State_t` cạnh các state hiện có tại vùng `Core/Src/vehicle_control.c:83-86`.
3. Gọi init trong `VehicleControl_Init()` tại `Core/Src/vehicle_control.c:696`. `VehicleControl_Stop()` chỉ reset yaw state; không được gọi lại `CurrentGovernor_Init()` ở mỗi lần stop vì thao tác đó sẽ xóa hard-fault latch.
4. Điểm chèn yaw correction chính xác nằm trong `VehicleControl_Update()` tại `Core/Src/vehicle_control.c:702`, **sau**:

```c
DifferentialControl_Compute(input->app.left_permille,
                            input->app.right_permille,
                            &command);
```

và **trước**:

```c
VehicleControl_UpdateChassis(&command);
```

Tức là giữa vị trí hiện tại `Core/Src/vehicle_control.c:718-722`:

```c
DifferentialControl_Compute(..., &command);       /* base command */
YawController_Update(..., &input->imu, &command);/* reduction-only */
VehicleControl_UpdateChassis(&command);           /* ramp/reversal/pivot */
```

Gate tại đây phải buộc correction về 0 nếu app brake/stale, IMU invalid/stale, đang reversal/pivot transition hoặc controller chưa được enable. Release đầu chỉ nên cho phép giảm magnitude một bên; không boost và không đổi dấu.

### 21.10. Địa chỉ chèn current governor

`Core/Src/current.c` vẫn chỉ làm acquisition và publish snapshot; không chèn limiter hoặc lệnh motor vào file này.

Trong `Core/Src/vehicle_control.c`, current governor cần hai điểm dùng:

1. Trong `VehicleControl_Update()` tại vùng `Core/Src/vehicle_control.c:702-722`:
   - tạo `CurrentSense_ControlFrame_t` từ `input->current` và bảng limit đã commissioning;
   - lấy applied state bằng `MotorBts_GetAppliedState()` để tạo `required_mask`;
   - gọi `CurrentGovernor_Update()` đúng một lần mỗi control cycle;
   - nếu `hard_fault_mask != 0`, gọi `MotorBts_EmergencyDisableAll()`, reset/freeze yaw controller rồi return;
   - nếu `controlled_stop != 0`, thay target bằng zero để đi qua ramp/reversal state machine.
2. Trong `VehicleControl_SafeReverse()` tại `Core/Src/vehicle_control.c:517`, sau vòng tính `output_percent[]` kết thúc ở dòng hiện tại `549` và trước vòng calibration/PWM bắt đầu ở dòng `551`, áp cùng một scale lên cả bốn motor:

```c
output_percent[index] = CurrentGovernor_ApplyScale(
    output_percent[index], current_result.common_scale_permille);
```

Như vậy thứ tự actuator thực tế là:

```text
base differential
 -> yaw correction
 -> VehicleControl_UpdateChassis / SafeReverse ramp
 -> CurrentGovernor_ApplyScale
 -> VehicleControl_ApplyMotorCalibration
 -> MotorBts_RunCommand
```

Không chèn current scaling sau `MotorBts_RunCommand()` và không scale riêng từng motor, vì sẽ làm thay đổi curvature đột ngột.

### 21.11. Các file không được trao thêm quyền ghi motor

- `Core/Src/imu_service.c`: chỉ acquisition/snapshot.
- `Core/Src/yaw_controller.c`: chỉ trả correction/result.
- `Core/Src/current.c`: chỉ acquisition/snapshot.
- `Core/Src/current_governor.c`: chỉ trả scale/fault/result.
- `Core/Src/differential_control.c`: chỉ ánh xạ và phân tích lệnh.
- `Core/Src/motor_bts.c`: vẫn là actuator driver cuối; không chứa PID/yaw/current policy.

Chỉ `Core/Src/vehicle_control.c` được hợp nhất các kết quả và quyết định lệnh cuối gửi đến `MotorBts_*`.

### 21.12. Địa chỉ test trước khi bật quyền điều khiển

Giữ các test đã có và bổ sung:

```text
tests/test_bno080.c               parser, signed Q, packet/sequence/truncation
tests/test_imu.c                  axis mapping, yaw, snapshot/freshness
tests/test_yaw_controller.c       sign tiến/lùi, reduction-only, anti-windup
tests/test_current_governor.c     common scale, hard fault, stale current
tests/test_vehicle_control.c      test tích hợp app + IMU + current + actuator
```

Các gate bắt buộc trước khi chuyển `shadow -> active`:

```text
BNO080 bus/pin/product identity              VERIFIED
body_from_sensor và dấu yaw tiến/lùi/pivot   VERIFIED
IMU calibration + freshness + fault fallback VERIFIED
current channel-to-motor mapping             VERIFIED
8 profile calibration dòng                   VERIFIED
soft/hard current limits                     VERIFIED
yaw correction reduction-only                TESTED
hard current luôn thắng yaw                  TESTED
brake/reversal/pivot invariants               TESTED
```

Nếu bất kỳ gate nào chưa đạt, vẫn được log IMU/current và chạy controller ở shadow, nhưng `VehicleControl_Update()` phải giữ nguyên command cơ sở như firmware hiện tại.

---

## 22. Checklist chèn code chính xác theo thứ tự

Số dòng dưới đây là số dòng của source tại ngày `2026-09-03`. Sau mỗi lần chèn, số dòng phía dưới sẽ dịch chuyển; khi đó phải tìm theo **câu neo trước/sau** được ghi trong từng bước, không bám mù vào số dòng cũ.

Không đảo thứ tự các bước. Làm và test xong bước trước mới sang bước sau.

### Bước 1 — Chốt phần cứng trước khi sửa C

**File:** `external_hbridge_f407.ioc`

Hiện file này không có SPI. Trước tiên phải xác nhận và ghi vào `docs/IMU.md`:

```text
SPI instance = ?
SCK/MISO/MOSI = ?
CS = ?
INT/DRDY = ?
RST = ?
WAKE = ?
sensor +X/+Y/+Z tương ứng body +X/+Y/+Z = ?
```

Sau khi xác nhận mới mở CubeMX, cấu hình SPI mode 3, EXTI và DMA. Generate code trước khi làm bước 2. Không lấy SPI2 và pin từ code tham chiếu cũ nếu chưa kiểm tra schematic/wiring của project này.

### Bước 2 — Tạo lớp sở hữu IMU, chưa đưa vào điều khiển

**Tạo mới:**

```text
Core/Inc/imu_service.h
Core/Src/imu_service.c
```

Thứ tự code bên trong `ImuService_Init()` phải là:

```c
/* 1 */ điền Bno080Transport_t bằng callback SPI/GPIO/time;
/* 2 */ Bno080_GetDefaultConfig(&bno_config);
/* 3 */ Bno080_Init(&s_bno080, &transport, &bno_config);
/* 4 */ Imu_GetDefaultConfig(&imu_config);
/* 5 */ điền body_from_sensor và bias đã đo;
/* 6 */ chỉ set axis_mapping_confirmed/calibration_valid khi đã verify;
/* 7 */ Imu_Init(&s_imu, &s_bno080, &imu_config);
```

`Imu_Start()` chưa gọi ở đây; nó được gọi trong IMU task sau khi scheduler chạy.

### Bước 3 — Thêm source vào build

**File:** `CMakeLists.txt`

**Dòng hiện tại:** `46-55`.

**Chèn sau dòng 53:**

```cmake
    Core/Src/current.c
```

**Chèn trước dòng 55:**

```cmake
)
```

**Kết quả phải thành:**

```cmake
    Core/Src/current.c
    Core/Src/bno080.c
    Core/Src/imu.c
    Core/Src/imu_service.c
```

Ở bước này chưa thêm `yaw_controller.c` và chưa thêm `current_governor.c`. Build phải qua với acquisition IMU độc lập trước.

### Bước 4 — Gọi init IMU đúng chỗ trong `main.c`

**File:** `Core/Src/main.c`

**Vị trí include:** dòng hiện tại `29-37`.

Chèn:

```c
#include "imu_service.h"
```

ngay **sau**:

```c
#include "current.h"
```

và **trước**:

```c
#include "vehicle_control.h"
```

**Vị trí peripheral init:** dòng hiện tại `99-108`. Hàm `MX_SPIx_Init()` do CubeMX sinh phải nằm **sau** `MX_DMA_Init()` và **trước** block `USER CODE BEGIN 2`. Ví dụ:

```c
MX_GPIO_Init();
MX_DMA_Init();
MX_SPIx_Init();          /* x là SPI đã chốt ở bước 1 */
...
MX_TIM8_Init();
/* USER CODE BEGIN 2 */
```

**Vị trí software init:** dòng hiện tại `111-115`. Chèn:

```c
ImuService_Init();
```

ngay **sau dòng 113**:

```c
Current_Init();
```

và **trước dòng 114**:

```c
AppControl_Init();
```

Kết quả:

```c
MotorBts_InitSafe();
Current_Init();
ImuService_Init();
AppControl_Init();
VehicleControl_Init();
```

### Bước 5 — Tạo IMU task trước, chưa sửa `VehicleControl_Update()`

**File:** `Core/Src/freertos.c`

1. Tại dòng hiện tại `29-32`, chèn `#include "imu_service.h"` **sau** `#include "current.h"` và **trước** `#include "vehicle_control.h"`.
2. Tại dòng hiện tại `63-69`, chèn block sau **sau dấu `};` của `currentTask_attributes`** và **trước comment `Private function prototypes`**:

```c
osThreadId_t imuTaskHandle;
const osThreadAttr_t imuTask_attributes = {
  .name = "imuTask",
  .stack_size = 512 * 4,       /* giá trị đầu để đo high-water mark */
  .priority = (osPriority_t) osPriorityAboveNormal,
};
```

3. Tại dòng hiện tại `110-112`, chèn lệnh tạo task **sau**:

```c
currentTaskHandle = osThreadNew(Current_Task, NULL,
                                &currentTask_attributes);
```

và **trước** `/* USER CODE END RTOS_THREADS */`:

```c
imuTaskHandle = osThreadNew(ImuService_Task, NULL,
                            &imuTask_attributes);
```

4. Trong file mới `Core/Src/imu_service.c`, `ImuService_Task()` chạy đúng thứ tự:

```c
Imu_Start(&s_imu);

for (;;)
{
    /* Bên trong hàm này, callback wait_data_ready mới chờ notification. */
    Imu_ProcessDataReady(&s_imu, bounded_timeout_ms);
}
```

Không gọi `ulTaskNotifyTake()` một lần ở ngoài rồi để callback `wait_data_ready` gọi nó lần nữa. Callback transport phải làm đúng một lần:

```text
nếu chân INT đang asserted: return BNO080_IO_OK ngay
nếu chưa asserted: chờ bit IMU_EVENT_DRDY với timeout hữu hạn
sau khi thức dậy: kiểm tra lại chân INT rồi mới return OK
```

Nếu SPI dùng DMA, dùng bit notification khác, ví dụ `IMU_EVENT_SPI_DONE`; không dùng chung một counting notification không phân biệt được DRDY với DMA complete.

Sau bước này chỉ kiểm tra Product ID, report rate, generation, age và quality. Chưa sửa command motor.

### Bước 6 — Nối DRDY sau khi IMU task đã tồn tại

**File:** `Core/Src/stm32f4xx_it.c`

Tại dòng hiện tại `24-28`, chèn:

```c
#include "imu_service.h"
```

**sau dòng 26** `#include "current.h"` và **trước dòng 27** `#include "usart6_log.h"`.

Handler EXTI chưa tồn tại trong source hiện tại. Sau khi CubeMX sinh handler, tìm đúng anchor:

```c
void EXTIx_IRQHandler(void)
```

Trong handler đó, thứ tự phải là:

```c
/* 1 */ kiểm tra/clear pending bit của đúng BNO080_INT;
/* 2 */ ImuService_DataReadyFromISR();
/* 3 */ portYIELD_FROM_ISR(...) nếu notification đánh thức task ưu tiên cao hơn;
```

Không chèn `Imu_ProcessDataReady()` vào ISR. `DMA2_Stream4_IRQHandler()` tại dòng hiện tại `182-190` đang dành cho ADC/current; không dùng lại stream này cho SPI IMU.

### Bước 7 — Đưa IMU/current snapshot vào input, vẫn chạy shadow

**File:** `Core/Inc/vehicle_control.h`

Tại dòng hiện tại `10-12`, chèn:

```c
#include "current.h"
#include "imu.h"
```

**sau** `#include "app_control.h"` và **trước** `#ifdef __cplusplus`.

Tại struct dòng hiện tại `17-20`, chèn ba field mới **sau** `AppControl_Snapshot_t app;` và **trước** `} VehicleControl_Input_t;`:

```c
CurrentSense_Snapshot_t current;
ImuSnapshot_t imu;
uint32_t control_timestamp_us;
```

**File:** `Core/Src/freertos.c`

Trong `StartDefaultTask()`, dòng hiện tại `137-140`, chèn việc lấy snapshot **sau**:

```c
(void)AppControl_GetSnapshot(&vehicle_input.app);
```

và **trước**:

```c
VehicleControl_Update(&vehicle_input);
```

Kết quả chính xác:

```c
AppControl_Process();
(void)AppControl_GetSnapshot(&vehicle_input.app);

vehicle_input.control_timestamp_us = ImuService_GetTimeUs();
Current_GetSnapshot(&vehicle_input.current);
ImuService_GetSnapshot(vehicle_input.control_timestamp_us,
                       &vehicle_input.imu);

VehicleControl_Update(&vehicle_input);
Usart6Log_Process();
```

Ở bước 7, `VehicleControl_Update()` chưa dùng hai field mới. Chỉ log và so sánh timestamp/generation; output motor phải giống baseline.

### Bước 8 — Thêm yaw controller vào build nhưng để shadow

**Tạo mới:**

```text
Core/Inc/yaw_controller.h
Core/Src/yaw_controller.c
tests/test_yaw_controller.c
```

Trong API của module này khai báo sẵn `YawController_SetActuatorFeedback()` để nhận scale/saturation của actuator từ control cycle trước. Ở bước 8-10 chưa gọi API này và controller phải coi authority mặc định là 100%.

Sau khi unit test qua, quay lại `CMakeLists.txt`, chèn:

```cmake
    Core/Src/yaw_controller.c
```

**sau** `Core/Src/imu_service.c` và **trước** dấu `)` của `target_sources`.

**File:** `Core/Src/vehicle_control.c`

1. Tại dòng hiện tại `5-9`, chèn `#include "yaw_controller.h"` **sau** `#include "motor_bts.h"` và **trước** `#include "task.h"`.
2. Tại dòng hiện tại `82-88`, thêm `static YawController_State_t s_yaw_controller;` **sau** `s_motor_states[]` và **trước** `s_last_update_tick`.
3. Trong `VehicleControl_Init()` dòng hiện tại `696-700`, chèn `YawController_Init(&s_yaw_controller);` **sau** `s_stop_logged = 0U;` và **trước** `VehicleControl_Stop();`.
4. Trong `VehicleControl_Stop()` dòng hiện tại `684-687`, gọi `YawController_Reset(&s_yaw_controller);` **sau khi reset chassis state** và **trước block log stop**.

Tại bước shadow, vẫn gọi `YawController_Update()` để lấy telemetry nhưng không copy candidate trở lại `command`.

### Bước 9 — Chèn yaw correction đúng giữa hai câu lệnh hiện tại

**File:** `Core/Src/vehicle_control.c`

**Hàm:** `VehicleControl_Update()`, dòng hiện tại `702-723`.

Điểm chèn duy nhất là khoảng trống tại **dòng 721**, tức là:

```c
/* dòng 718-720: GIỮ NGUYÊN */
DifferentialControl_Compute(input->app.left_permille,
                            input->app.right_permille,
                            &command);

/* DÒNG 721: CHÈN YAW CONTROLLER Ở ĐÂY */
YawController_Update(&s_yaw_controller,
                     &input->imu,
                     input->control_timestamp_us,
                     &command,
                     &yaw_result);

if (s_yaw_active != 0U && yaw_result.authorized != 0U)
{
    command = yaw_result.corrected_command;
}

/* dòng 722: GIỮ SAU yaw controller */
VehicleControl_UpdateChassis(&command);
```

`s_yaw_active` phải mặc định bằng 0. Trước khi active, log `base command`, `candidate`, `corrected`, `r_ref`, `r_measured`, `quality`, `generation` và saturation tối thiểu qua các bài test mặt đất.

Ở bước 9 chưa truyền `CurrentGovernor_Result_t` vào yaw controller. Phản hồi saturation/current chỉ được nối ở bước 11, sau khi current governor đã build và chạy shadow thành công.

### Bước 10 — Truyền current snapshot xuống đúng chỗ sau ramp

**File:** `Core/Src/vehicle_control.c`

Sửa prototype tại dòng hiện tại `517-521` từ:

```c
static void VehicleControl_SafeReverse(
    const DifferentialControl_Command_t *command,
    uint32_t accel_rate_percent_s,
    uint32_t decel_rate_percent_s,
    int32_t output_limit)
```

thành:

```c
static void VehicleControl_SafeReverse(
    const DifferentialControl_Command_t *command,
    uint32_t accel_rate_percent_s,
    uint32_t decel_rate_percent_s,
    int32_t output_limit,
    const CurrentSense_Snapshot_t *current_snapshot)
```

Sửa prototype tại dòng hiện tại `578-579` từ:

```c
static void VehicleControl_UpdateChassis(
    const DifferentialControl_Command_t *requested_command)
```

thành:

```c
static void VehicleControl_UpdateChassis(
    const DifferentialControl_Command_t *requested_command,
    const CurrentSense_Snapshot_t *current_snapshot)
```

Sau đó truyền `current_snapshot` làm tham số cuối tại cả bốn chỗ gọi `VehicleControl_SafeReverse()` hiện ở dòng `600`, `616`, `632`, `649`.

Trong `VehicleControl_SafeReverse()`, tạm thêm:

```c
(void)current_snapshot; /* xóa ở bước 11 khi governor bắt đầu dùng nó */
```

ngay sau block kiểm tra `if (command == NULL)`. Việc này giữ build sạch với `-Wextra` giữa bước 10 và bước 11.

Cuối cùng, tại dòng hiện tại `722`, đổi:

```c
VehicleControl_UpdateChassis(&command);
```

thành:

```c
VehicleControl_UpdateChassis(&command, &input->current);
```

Bước này chỉ truyền dữ liệu, chưa enable governor.

### Bước 11 — Thêm current governor sau ramp, trước calibration/PWM

Chỉ làm bước này sau khi `Current_SetCalibration()` (`Core/Src/current.c:545`) và `Current_SetMappingConfirmed()` (`Core/Src/current.c:582`) đã được commissioning.

1. Trong `CMakeLists.txt`, thêm `Core/Src/current_governor.c` **sau** `Core/Src/current.c` và **trước** `Core/Src/bno080.c`.
2. Trong `Core/Src/vehicle_control.c`, thêm `#include "current_governor.h"` **sau** `#include "differential_control.h"` và **trước** `#include "motor_bts.h"`.
3. Sau bảng `s_motor_calibration[]` kết thúc tại dòng hiện tại `80` và trước `static uint8_t s_stop_logged;` tại dòng `82`, thêm bảng limit:

```c
static const CurrentSense_ProtectionLimits_t
    s_current_limits[CURRENT_MOTOR_COUNT] =
{
    /* M1 */ { .soft_limit_ma = 0U, .hard_limit_ma = 0U },
    /* M2 */ { .soft_limit_ma = 0U, .hard_limit_ma = 0U },
    /* M3 */ { .soft_limit_ma = 0U, .hard_limit_ma = 0U },
    /* M4 */ { .soft_limit_ma = 0U, .hard_limit_ma = 0U }
};
```

Giá trị 0 chỉ là trạng thái disable an toàn; thay bằng số đã đo trước khi active, không đoán theo datasheet danh nghĩa.

4. Tại vùng state dòng hiện tại `82-88`, thêm:

```c
static CurrentGovernor_State_t s_current_governor;
static CurrentGovernor_Result_t s_current_result;
static uint8_t s_current_governor_active; /* mặc định 0 */
```

5. Trong `VehicleControl_Init()` dòng hiện tại `696-700`, gọi:

```c
CurrentGovernor_Init(&s_current_governor);
s_current_result = (CurrentGovernor_Result_t){0};
s_current_result.common_scale_permille =
    CURRENT_GOVERNOR_SCALE_PERMILLE;
s_current_governor_active = 0U;
```

**Chèn sau** `YawController_Init()` và **trước** `VehicleControl_Stop()`.

6. Không gọi `CurrentGovernor_Init()` trong `VehicleControl_Stop()`. Hard-fault latch chỉ được clear bằng một API reset fault riêng, khi app đang brake, mọi output bằng 0 và nguyên nhân lỗi đã hết.

7. Trong `VehicleControl_SafeReverse()`, xóa dòng tạm `(void)current_snapshot;`, rồi dùng chính `dt_ms` đã tính tại dòng hiện tại `535`; không gọi `VehicleControl_GetDeltaMs()` lần thứ hai.

Điểm chèn `CurrentGovernor_Update()` là **sau vòng for thứ nhất kết thúc tại dòng 549** và **trước vòng for thứ hai bắt đầu tại dòng 551**:

```c
/* output_percent[] vừa được ramp/reversal xử lý xong */
VehicleControl_UpdateCurrentGovernor(current_snapshot,
                                     dt_ms,
                                     &s_current_result);

if (s_current_result.hard_fault_mask != 0U)
{
    YawController_Reset(&s_yaw_controller);
    MotorBts_EmergencyDisableAll();
    return; /* bắt buộc: không được chạy xuống vòng ghi PWM */
}

for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
{
    if (dynamic_brake[index] == 0U)
    {
        output_percent[index] = CurrentGovernor_ApplyScale(
            output_percent[index],
            s_current_result.common_scale_permille);
    }
}

/* Phản hồi saturation này được yaw controller dùng ở control cycle sau. */
YawController_SetActuatorFeedback(
    &s_yaw_controller,
    s_current_result.common_scale_permille,
    s_current_result.soft_limit_mask,
    s_current_result.controlled_stop);

/* vòng for calibration/PWM hiện tại bắt đầu sau đoạn này */
for (index = 0U; index < MOTOR_BTS_MOTOR_COUNT; ++index)
{
    ...
}
```

`VehicleControl_UpdateCurrentGovernor()` nên được đặt **sau hàm `VehicleControl_GetMotorTarget()` kết thúc ở dòng 515** và **trước định nghĩa `VehicleControl_SafeReverse()` ở dòng 517**. Helper này làm đúng thứ tự:

```text
MotorBts_GetAppliedState() cho 4 motor
 -> tạo required_mask từ mode DRIVE và abs(command) >= CURRENT_MIN_OBSERVABLE_PERMILLE
 -> copy current_snapshot vào CurrentSense_ControlFrame_t
 -> nạp limit đã verify
 -> frame.governor_enabled = s_current_governor_active
 -> CurrentGovernor_Update(...)
```

Tất cả bốn motor phải dùng cùng `common_scale_permille` của một control cycle.

### Bước 12 — Thứ tự logic cuối cùng trong `VehicleControl_Update()`

Sau khi hoàn thành các bước trên, `VehicleControl_Update()` phải có thứ tự đọc từ trên xuống như sau:

```c
/* 1. App safety gate */
if (input/app invalid hoặc brake)
{
    VehicleControl_Stop();
    return;
}

/* 2. Nếu current cycle trước yêu cầu controlled stop, không tạo target mới */
if (s_current_result.controlled_stop != 0U)
{
    VehicleControl_MakeZeroCommand(&command);
    YawController_Reset(&s_yaw_controller);
}
else
{
    /* 3. Base differential */
    DifferentialControl_Compute(..., &command);

    /* 4. IMU yaw correction; ban đầu shadow, sau đó reduction-only */
    YawController_Update(..., &command, &yaw_result);
    if (s_yaw_active != 0U && yaw_result.authorized != 0U)
    {
        command = yaw_result.corrected_command;
    }
}

/* 5. Ramp/reversal/pivot; bên trong hàm này mới update/apply current governor */
VehicleControl_UpdateChassis(&command, &input->current);
```

Bên trong `VehicleControl_SafeReverse()` thứ tự bắt buộc là:

```text
VehicleControl_UpdateMotor() cho cả 4 motor
 -> CurrentGovernor_Update() đúng 1 lần
 -> nếu hard fault: EmergencyDisableAll + return
 -> CurrentGovernor_ApplyScale() cùng scale cho cả 4
 -> VehicleControl_ApplyMotorCalibration()
 -> MotorBts_RunCommand()
```

Đây là hai cặp câu neo quan trọng nhất:

```text
DifferentialControl_Compute
    < CHÈN YAW CONTROLLER Ở GIỮA >
VehicleControl_UpdateChassis

VehicleControl_UpdateMotor
    < CHÈN CURRENT GOVERNOR Ở GIỮA >
VehicleControl_ApplyMotorCalibration / MotorBts_RunCommand
```

### Bước 13 — Trình tự bật quyền, không bật cả hai cùng lúc

Thứ tự commissioning bắt buộc:

```text
1. IMU acquisition chạy riêng
2. kiểm tra Product ID, timestamp, sequence, freshness
3. xác nhận hệ trục và dấu yaw
4. yaw controller chạy shadow, command motor vẫn là baseline
5. current acquisition + mapping + 8 profile calibration
6. current governor chạy shadow, chỉ log scale/fault
7. bật current governor active trước, yaw vẫn shadow
8. test hard limit, stale current, brake, reversal, pivot
9. bật yaw reduction-only chỉ ở chế độ đi thẳng
10. test mất IMU và saturation/anti-windup
11. mới mở yaw assist cho turn
12. pivot để cuối cùng
```

Không được chuyển thẳng từ “đã đọc được IMU” sang bật đồng thời yaw correction và current limiting. Nếu xe có phản ứng sai sẽ không phân biệt được lỗi đến từ hệ trục IMU, dấu correction, current mapping, limit hay state machine.
