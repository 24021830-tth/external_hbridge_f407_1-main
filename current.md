# HỢP ĐỒNG KIẾN TRÚC PHẢN HỒI DÒNG CHO XE VI SAI 4 ĐỘNG CƠ

> Trạng thái tài liệu: **HỢP ĐỒNG THIẾT KẾ + TRIỂN KHAI SHADOW G1/G2**  
> Phiên bản: `0.2-shadow-implementation`  
> Ngày lập: `2026-08-31`  
> Ngày cập nhật triển khai: `2026-09-02`  
> Dự án: `external_hbridge_f407` — STM32F407VGT6, FreeRTOS, 4 cầu H dùng BTS7960  
> Phạm vi thay đổi hiện tại: **đã triển khai acquisition + processing + governor nhưng mặc định khóa ở shadow mode cho đến khi Gate G0 đạt**

---

## 0. Tuyên bố ràng buộc

Tài liệu này là hợp đồng kỹ thuật giữa người sở hữu xe và người triển khai firmware. Mọi phần triển khai phản hồi dòng sau này **phải** tuân theo tài liệu này hoặc phải có một thay đổi được ghi nhận, giải thích và phê duyệt trước.

Các từ khóa được dùng theo nghĩa sau:

- **BẮT BUỘC / PHẢI / KHÔNG ĐƯỢC**: yêu cầu cứng. Vi phạm yêu cầu này làm hạng mục không được nghiệm thu.
- **NÊN / KHÔNG NÊN**: mặc định phải làm; chỉ được làm khác khi có số liệu đo hoặc lý do kỹ thuật được ghi lại.
- **CÓ THỂ**: lựa chọn không bắt buộc.
- **TBD**: dữ kiện chưa biết và phải được đo/xác nhận. Không được thay TBD bằng phỏng đoán rồi đưa xe vào vận hành.
- **Gate**: cổng nghiệm thu. Không được chuyển sang giai đoạn sau nếu gate hiện tại chưa đạt.

Quy tắc quan trọng nhất:

> **Phản hồi dòng không phải phản hồi tốc độ.** Dòng điện chỉ được dùng làm đại lượng gần với mô-men/tải, làm giới hạn an toàn và làm tín hiệu chẩn đoán. Nếu chưa có encoder/IMU thì firmware không được tuyên bố đã đóng vòng tốc độ, giữ hướng tuyệt đối, đo độ trượt chính xác hoặc xác nhận xe đã đứng yên.

---

## 1. Mục tiêu, phạm vi và tiêu chí thành công

### 1.1. Mục tiêu bắt buộc

Kiến trúc sau khi hoàn thành phải đạt các mục tiêu sau:

1. Thu nhận được bốn tín hiệu dòng tương ứng M1, M2, M3, M4 với thứ tự xác định, có timestamp, có trạng thái hợp lệ và không dùng polling blocking.
2. Chuyển đổi ADC sang đại lượng dòng đã hiệu chuẩn riêng cho từng motor và từng chiều quay; không dùng một hệ số danh định duy nhất cho cả tám trường hợp motor × chiều.
3. Tách ba mục đích sử dụng dòng:
   - đường bảo vệ nhanh;
   - đường giới hạn mô-men/tải trong vòng điều khiển;
   - đường thống kê/chẩn đoán chậm.
4. Kết hợp giới hạn dòng với điều khiển vi sai mà không làm sai tỷ lệ trái–phải một cách bất ngờ.
5. Giảm giật khi tăng tốc, giảm tốc, rẽ, đổi chiều và quay tại chỗ bằng giới hạn vận tốc thay đổi lệnh và giới hạn jerk, không chỉ bằng lọc dòng.
6. Bảo vệ được motor, cầu công suất, dây, đầu nối, pin và PCB theo phần tử yếu nhất của hệ thống; không lấy con số quảng cáo hoặc dòng giới hạn điển hình của chip BTS7960 làm dòng an toàn của toàn xe.
7. Nhận biết được các tình huống quá tải, nghi kẹt, cảm biến lỗi, ADC/DMA lỗi và dữ liệu quá hạn; đưa xe về trạng thái an toàn xác định.
8. Có telemetry đủ để tái dựng một sự kiện: lệnh đầu vào, lệnh sau ramp, duty thực, dòng, hệ số giới hạn, mode và fault.
9. Có bộ kiểm thử host và quy trình thử phần cứng theo từng gate trước khi cho bánh chạm đất.

### 1.2. Mục tiêu trải nghiệm chuyển động

Các cải thiện mong muốn được diễn giải chính xác như sau:

- **Đi thẳng mượt hơn**: duty trái/phải thay đổi liên tục theo giới hạn ramp/jerk; giới hạn dòng không làm một bên giảm riêng gây đổi hướng đột ngột.
- **Rẽ mượt hơn**: giữ tỷ lệ lệnh trái/phải khi toàn hệ thống phải giảm công suất; tránh cắt đỉnh từng bên độc lập.
- **Quay tại chỗ mượt hơn**: giữ state machine vào/ra pivot đối xứng, gia tốc pivot thấp, có current budget phù hợp với ma sát trượt ngang của lốp.
- **Vượt địa hình tốt hơn**: tránh tăng duty quá nhanh làm dòng vọt, duy trì mô-men trong biên an toàn và phát hiện tải tăng. Hệ thống không được tự ý tăng vượt lệnh người lái hoặc tự đảo chiều để “lấy đà”.

### 1.3. Ngoài phạm vi phiên bản phản hồi dòng đầu tiên

Phiên bản đầu không cam kết:

- điều khiển chính xác vận tốc m/s;
- giữ yaw/góc hướng khi mặt đường hai bên khác nhau;
- đo slip ratio hoặc traction tuyệt đối;
- odometry;
- torque vectoring chủ động làm tăng duty một bánh vượt lệnh cơ sở;
- tự thoát kẹt bằng đảo chiều/tăng ga tự động;
- thay thế cầu chì, bảo vệ phần cứng, bảo vệ nhiệt hoặc giới hạn dòng nội tại của BTS7960.

Muốn cam kết các mục tiêu này phải bổ sung encoder bánh và tối thiểu IMU yaw-rate, sau đó lập một hợp đồng mở rộng riêng.

---

## 2. Kết quả khảo sát bắt buộc của project hiện tại

### 2.1. Luồng điều khiển đang tồn tại

Luồng hiện tại đã được xác nhận từ source:

`USART2 IRQ` → giải mã DC3W2 → `AppControl_Snapshot` → `VehicleControl_Update` → `DifferentialControl_Compute` → state machine ramp/pivot/đổi chiều → `MotorBts_RunCommand` → PWM + INH.

Các đặc điểm phải được bảo toàn:

- Payload CONTROL hiện đã chứa trực tiếp `left_permille` và `right_permille`; firmware **không được trộn vi sai lần thứ hai**.
- Timeout lệnh hiện tại là 100 ms.
- Mapping logic hiện tại:
  - M1: phải trước;
  - M2: phải sau;
  - M3: trái trước;
  - M4: trái sau.
- State machine hiện tại đã có ramp theo thời gian, khoảng phanh động 100 ms và nhánh pivot đối xứng giới hạn 35%.
- Một `defaultTask` đang chạy chu kỳ danh nghĩa 1 ms nhưng đồng thời xử lý communication, vehicle control và log. Đây chưa phải scheduling đủ xác định cho vòng phản hồi dòng.

### 2.2. Mapping PWM/INH được coi là nguồn sự thật hiện tại

Nguồn sự thật phải ưu tiên theo thứ tự: pin thực tế đã đo → `.ioc` và macro generated → cấu hình được dùng trong `motor_bts.c` → comment/tài liệu cũ.

| Motor | Vị trí logic | PWM A | PWM B | INH A | INH B | PWM danh nghĩa |
|---|---|---|---|---|---|---:|
| M1 | phải trước | TIM1_CH1 / PE9 | TIM1_CH2 / PE11 | PE10 | PE12 | 14 kHz |
| M2 | phải sau | TIM1_CH3 / PE13 | TIM1_CH4 / PE14 | PB10 | PB12 | 14 kHz |
| M3 | trái trước | TIM3_CH1 / PA6 | TIM3_CH2 / PA7 | PC4 | PC5 | 14 kHz |
| M4 | trái sau | TIM2_CH3 / PA2 | TIM2_CH4 / PA3 | PB0 | PB1 | 14 kHz |

Ba timer đều tạo 14 kHz theo clock/ARR hiện tại, nhưng đang được khởi động riêng và **chưa có bằng chứng chúng đồng bộ pha**.

Một số comment/tài liệu cũ trong repository ghi M1 INH là PE5/PE3, M2 là PB9/PB8 hoặc ghi M3 dùng TIM2_CH1/CH2. Các thông tin đó không khớp `.ioc`/`main.h` hiện tại và **không được dùng để đấu dây hay thiết kế tiếp** nếu chưa đo lại.

### 2.3. Trạng thái ADC/DMA hiện tại

Project đã dành bốn chân analog:

| Chân | ADC channel | Motor tương ứng |
|---|---:|---|
| PA0 | ADC1_IN0 | **TBD-MAP-ADC-01** |
| PA1 | ADC1_IN1 | **TBD-MAP-ADC-02** |
| PC0 | ADC1_IN10 | **TBD-MAP-ADC-03** |
| PC1 | ADC1_IN11 | **TBD-MAP-ADC-04** |

Tuy nhiên trạng thái generated code hiện tại là:

- ADC1 12 bit, right-aligned;
- scan disabled;
- sequencer chỉ có rank 1 = channel 0;
- single conversion, software trigger;
- sampling time 3 ADC cycles;
- DMA2 Stream 4, channel 0, circular, half-word, memory increment;
- DMA request sau chuỗi đang ở chế độ limited;
- IRQ DMA2 Stream 4 đã bật priority 5 nhưng handler rỗng;
- chưa có buffer ADC, chưa có start sequence và chưa có publisher dữ liệu dòng.

Kết luận ràng buộc: bốn GPIO analog tồn tại **không đồng nghĩa bốn kênh dòng đang được đọc**. Phần acquisition phải được thiết kế và kiểm chứng như một subsystem mới.

---

## 3. Các dữ kiện phần cứng bắt buộc phải chốt trước khi code

Không được bắt đầu Gate G1 nếu bảng dưới chưa được điền bằng sơ đồ mạch, đo continuity hoặc đo oscilloscope/DMM.

| ID | Dữ kiện cần chốt | Cách chứng minh | Trạng thái |
|---|---|---|---|
| HW-01 | PA0/PA1/PC0/PC1 lần lượt thuộc M1..M4 nào | continuity + kích riêng từng motor | TBD |
| HW-02 | Tín hiệu là BTS7960 IS, shunt+amplifier, Hall sensor hay loại khác | schematic/part number | TBD |
| HW-03 | Mỗi motor có hai IS riêng, đã cộng/OR về một đường, hay chỉ nối một IS | schematic + continuity | TBD |
| HW-04 | Giá trị `R_IS`, divider, RC filter, điện trở series và clamp ADC | đo linh kiện/schematic | TBD |
| HW-05 | Điện áp cực đại tại từng chân ADC khi tải lớn và khi BTS báo fault | oscilloscope có probe phù hợp | TBD |
| HW-06 | Khả năng đo ở cả chiều tiến và lùi | chạy riêng từng chiều trên giá kê | TBD |
| HW-07 | Dòng liên tục/đỉnh cho phép của motor | datasheet/đo stall có kiểm soát | TBD |
| HW-08 | Ampacity của PCB, module BTS, dây, terminal, cầu chì và pin | BOM/datasheet/thiết kế | TBD |
| HW-09 | Điện áp pin min/nominal/max và sụt áp khi tải | đo thực tế | TBD |
| HW-10 | Khối lượng xe, bán kính bánh, tỷ số truyền, bề rộng vệt bánh | đo thực tế | TBD |
| HW-11 | Chiều dương thực tế của từng motor | test bánh kê cao | TBD |
| HW-12 | Tần số/pha PWM thực tế trên tám đầu vào BTS | oscilloscope/logic analyzer | TBD |

### 3.1. Điều kiện chặn đặc biệt đối với BTS7960 IS

Một cầu H đầy đủ dùng hai BTS7960 half-bridge. Theo datasheet BTS7960:

- IS là ngõ ra kết hợp current sense và fault;
- trong chế độ bình thường, IS chỉ tỷ lệ với **dòng thuận qua high-side đang active**;
- khi high-side không active hoặc dòng chạy ngược, IS gần như chỉ còn leakage;
- khi fault, IS chuyển sang dòng báo lỗi, không còn đơn thuần là dòng tải;
- hệ số danh định `k_ILIS = I_L / I_IS` khoảng 8500 nhưng sai số min/max rất rộng, đặc biệt ở dòng thấp.

Do cách điều khiển hiện tại giữ cả hai INH cao, một input PWM và input còn lại bằng 0:

- ở pha ON, một high-side và low-side của half-bridge đối diện tạo đường drive; chỉ IS của half-bridge có high-side đang chạy mới mang thông tin dòng thuận;
- ở pha OFF active-freewheel qua low-side, BTS IS có thể về gần 0 dù dòng cuộn dây chưa về 0;
- khi phanh động bằng cách kéo hai đầu motor xuống low-side, BTS IS **không đo được đầy đủ dòng phanh**;
- khi tái sinh/back-drive hoặc dòng đổi chiều qua phần tử không được sense, BTS IS không phải cảm biến dòng hai chiều hoàn chỉnh.

Vì vậy:

1. Nếu mỗi ADC chỉ nối IS của một half-bridge, hệ thống sẽ mù một chiều quay và **không đạt hợp đồng**.
2. Nếu hai IS được nối chung/cộng về một ADC, topology đó phải được chứng minh an toàn về điện và phải hiệu chuẩn riêng theo chiều vì hai die khác nhau.
3. Nếu cần biết dòng thật trong cả drive, freewheel, phanh và regeneration, phương án bắt buộc là cảm biến dòng hai chiều độc lập trên đường motor hoặc từng nhánh thích hợp; không được gọi BTS IS là phép đo đó.

### 3.2. Ràng buộc bảo vệ chân ADC

Chân ADC STM32 không được nhận điện áp vượt phạm vi cho phép của MCU trong mọi trạng thái bình thường và fault.

Với BTS7960 IS và điện trở xuống mass:

\[
V_{IS}=I_{IS}R_{IS}=\frac{I_L}{k_{ILIS}}R_{IS}
\]

Datasheet cho biết dòng IS ở fault có thể lên tới 7 mA. Ví dụ `R_IS = 1 kΩ` có thể tạo đến 7 V ở fault; con số này không an toàn để đưa thẳng vào ADC 3,3 V. Giá trị 1 kΩ còn cho khoảng 3,53 V tại 30 A nếu dùng `k = 8500` danh định.

Do đó phần cứng **bắt buộc** phải thỏa tất cả điều kiện:

- có chứng minh giới hạn điện áp ADC trong worst-case, không chỉ nominal;
- có điện trở series và clamp/divider phù hợp hoặc chọn `R_IS` phù hợp;
- RC không được làm source impedance quá cao so với sampling time ADC;
- mass sense, mass BTS và VSSA phải có đường hồi dòng hợp lý, tránh ground bounce công suất;
- mọi threshold ADC phải dựa trên điện áp đo thực và sai số linh kiện;
- không dùng diode bảo vệ nội MCU làm phần tử clamp vận hành thường xuyên.

Nếu phần cứng hiện tại không đạt, firmware **không được dùng tín hiệu đó trước khi mạch được sửa**.

---

## 4. Cơ sở lý thuyết bắt buộc

### 4.1. Mô hình motor DC tối thiểu

Mô hình điện–cơ cơ bản:

\[
V_a = R_a i + L_a\frac{di}{dt} + K_e\omega_m
\]

\[
\tau_m = K_t i
\]

\[
J\frac{d\omega_m}{dt}=\tau_m-\tau_{load}-B_m\omega_m
\]

Ý nghĩa bắt buộc khi thiết kế:

- dòng gần tỷ lệ với mô-men motor, nhưng hệ số còn phụ thuộc motor, nhiệt độ, gearbox và hiệu suất;
- cùng một dòng có thể xuất hiện ở nhiều vận tốc khác nhau;
- lúc stall, back-EMF gần 0 nên dòng có xu hướng lớn;
- dòng cao có thể là tăng tốc, leo dốc, quay tại chỗ, kẹt cơ khí hoặc fault; dòng cao **không tự nó chứng minh kẹt**;
- dòng thấp có thể là tải nhẹ hoặc bánh đang trượt; dòng thấp **không tự nó chứng minh trượt**.

### 4.2. Động học vi sai

Với vận tốc hai bên `v_L`, `v_R` và bề rộng vệt bánh `B`:

\[
v=\frac{v_R+v_L}{2}, \qquad \omega_z=\frac{v_R-v_L}{B}
\]

Tín hiệu dòng không thay thế `v_L`, `v_R`. Vì vậy current feedback phải nằm ở lớp **giới hạn effort/torque** bên dưới command shaping, không được sửa công thức động học bằng cách gán dòng thành vận tốc.

### 4.3. Bảo toàn độ cong khi giới hạn công suất

Khi hai lệnh trái/phải đã được app tính, tỷ lệ giữa chúng quyết định quỹ đạo mong muốn. Nếu một bên bị cắt riêng, bán kính cua đổi đột ngột.

Kiến trúc mặc định phải dùng hệ số giảm chung `g_common` trong khoảng 0..1:

\[
u'_L=g_{common}u_L, \qquad u'_R=g_{common}u_R
\]

Trong đó hệ số chung lấy theo ràng buộc nghiêm nhất của bốn motor trong chế độ vận hành bình thường. Như vậy feedback dòng có quyền **giảm biên độ tổng**, nhưng không tự ý xoay vector lệnh trái–phải.

Ngoại lệ duy nhất là đường fault: khi an toàn bị vi phạm, ưu tiên dừng xe cao hơn bảo toàn quỹ đạo.

### 4.4. Năng lượng nhiệt và quá tải theo thời gian

Không được dùng duy nhất một threshold tức thời. Ít nhất phải có:

- giới hạn đỉnh rất ngắn;
- giới hạn mềm dùng cho control;
- giới hạn liên tục;
- mô hình tích lũy nhiệt hoặc `I²t`;
- hysteresis và thời gian phục hồi.

Một trạng thái tải nhiệt tương đối có thể dựa trên:

\[
E[k+1]=\max\left(0,E[k]+(I^2-I_{cont}^2)\Delta t-\lambda E[k]\Delta t\right)
\]

Đây là thermal proxy, không phải nhiệt độ junction thật. Tham số phải được hiệu chỉnh theo motor/driver, không được sao chép tùy ý.

---

## 5. Phương án kiến trúc được chọn

### 5.1. Quyết định tổng thể

Phương án được chọn cho phiên bản đầu:

> **Điều khiển vi sai open-loop theo lệnh trái/phải + command shaping giới hạn jerk + current-envelope governor chỉ có quyền giảm effort + bảo vệ dòng nhiều tầng + chẩn đoán tải/kẹt có điều kiện.**

Không chọn điều khiển “bốn motor cùng dòng” và không chọn PI dòng đặt trực tiếp từ joystick.

### 5.2. Lý do chọn

- Phù hợp với dữ liệu hiện có: app đã gửi lệnh trái/phải, chưa có encoder/IMU.
- Dòng hữu ích để bảo vệ và ước lượng tải nhưng không đủ làm feedback tốc độ.
- Hệ số giảm chung giữ quỹ đạo vi sai ổn định hơn việc mỗi motor tự cắt duty.
- Jerk/ramp giải quyết nguồn gây giật chính là bước lệnh và đổi chiều.
- Phân tầng cho phép bật lần lượt: đo shadow → bảo vệ → giới hạn mềm → thích nghi, giảm rủi ro.

### 5.3. Các phương án bị loại hoặc hoãn

| Phương án | Quyết định | Lý do |
|---|---|---|
| Ép I(M1)=I(M2)=I(M3)=I(M4) | Loại | tải bánh khác nhau là bình thường; dễ làm bánh nhẹ tăng duty và trượt |
| Dùng dòng thay encoder để điều khiển tốc độ | Loại | không quan sát được vận tốc duy nhất từ dòng |
| Mỗi motor tự PI dòng và tự sửa duty không phối hợp | Loại | làm sai quỹ đạo/yaw khi một motor bị giới hạn |
| Chỉ lọc moving average rồi cắt threshold | Loại | che peak, không xét thời gian/nhiệt/mode và không bảo toàn vi sai |
| Tăng duty tự động khi thấy dòng cao | Loại | dòng cao đã thể hiện tải/effort cao; tăng tiếp có thể gây hỏng |
| Tự đảo chiều khi nghi kẹt | Loại | nguy hiểm và current-only không đủ xác nhận môi trường |
| Torque vectoring dương giữa motor trước/sau | Hoãn | cần encoder/IMU hoặc ít nhất thử nghiệm traction có kiểm soát |
| Vòng tốc độ/yaw cascade | Hoãn | chỉ mở khi có encoder + IMU |

---

## 6. Kiến trúc module bắt buộc

### 6.1. Sơ đồ lớp

```text
DC3W2 / App command
        │
        ▼
Command Supervisor
(valid, timeout, brake, sequence)
        │
        ▼
Motion Mode + Differential Command
(normal / arc / pivot / reversal / stop)
        │
        ▼
Jerk-limited Command Shaper
(left/right target → left/right smooth)
        │
        ▼
Current Envelope Governor ◄──────── Current Snapshot
(common scale, ramp adaptation, I²t)          ▲
        │                                     │
        ▼                              Current Processing
Safety Arbiter ◄──────── fault ─────── (calibration/filter/validity)
        │                                     ▲
        ▼                                     │
Motor Actuator (sole normal writer)      ADC + DMA acquisition
        │
        ▼
4 × BTS H-bridge
```

### 6.2. Trách nhiệm từng module

#### A. Current Acquisition

Chỉ chịu trách nhiệm ADC, trigger, DMA, thứ tự sample, buffer, timestamp, overrun và freshness. Không chứa thuật toán vi sai, không log text và không gọi API điều khiển motor bình thường.

#### B. Current Processing/Monitor

Chịu trách nhiệm:

- raw ADC → voltage/current;
- calibration theo motor × chiều;
- xác định sample có nằm trong cửa sổ sense hợp lệ hay không;
- lọc fast/average/RMS;
- phát hiện rail, stuck, sequence lỗi, stale;
- tạo immutable snapshot có generation counter.

#### C. Command Shaper

Chịu trách nhiệm deadband nếu cần, ramp, giới hạn acceleration/deceleration và jerk. Không truy cập ADC trực tiếp.

#### D. Current Envelope Governor

Chịu trách nhiệm tạo hệ số giảm, giới hạn current budget, tích lũy I²t và phối hợp bốn motor. Governor không được tăng duty vượt lệnh đã được command shaper cấp.

#### E. Safety Arbiter

Là nơi duy nhất quyết định trạng thái DRIVE, DERATE, CONTROLLED_STOP, EMERGENCY_STOP và FAULT_LATCHED. Mọi fault có priority rõ ràng.

#### F. Motor Actuator

Là chủ sở hữu bình thường duy nhất của CCR/INH. Một primitive emergency-disable riêng, idempotent và ISR-safe có thể được phép cắt toàn bộ INH; ngoài ngoại lệ đó, không module/ISR nào được ghi trực tiếp CCR/INH.

#### G. Telemetry

Chỉ đọc snapshot, decimate và gửi non-blocking. Telemetry chậm hoặc đầy queue không được chặn acquisition/control.

---

## 7. Hợp đồng thu nhận ADC/DMA

### 7.1. Kênh và thứ tự

- ADC1 phải chạy regular scan đủ 4 rank.
- Rank order phải trùng một bảng mapping duy nhất M1→M4 và có static/runtime assertion hợp lý.
- DMA buffer phải lặp đúng mẫu `[M1, M2, M3, M4]` cho mỗi scan frame.
- Không module nào được hiểu index ADC bằng số rải rác; mapping phải tập trung tại một nơi.
- Mỗi buffer publish phải có `generation`, timestamp, số scan và cờ overrun.

### 7.2. Trigger và tần số

Yêu cầu thiết kế mặc định:

- dùng trigger phần cứng từ timer, không khởi phát từng conversion bằng task 1 ms;
- ADC scan và PWM phải có quan hệ thời gian xác định hoặc có chiến lược oversampling chứng minh không alias;
- mục tiêu ban đầu là tối thiểu 4 complete scan/PWM; ưu tiên 8 complete scan/PWM nếu ADC settling và CPU/DMA budget cho phép;
- với PWM 14 kHz, target khảo sát là 56–112 k complete scan/s, tương đương 224–448 k conversion/s cho 4 kênh;
- con số cuối phải được chốt bằng timing budget và oscilloscope, không chỉ dựa vào CubeMX.

Ba PWM timer hiện cùng tần số nhưng chưa đồng bộ pha. Trước khi dùng PWM-state gating, triển khai phải thực hiện một trong hai phương án và ghi rõ lựa chọn:

1. **Phương án ưu tiên**: đồng bộ carrier TIM1/TIM2/TIM3 và lấy trigger ADC từ timebase liên quan xác định;
2. **Phương án dự phòng**: oversample đủ cao, chứng minh coverage qua mọi pha PWM và tính được duty-weighted current mà không alias cố định.

Không được giả định “cùng 14 kHz” đồng nghĩa “cùng pha”.

### 7.3. Sampling time và analog settling

Sampling time 3 cycles hiện tại không được giữ theo quán tính. Phải tính theo source impedance, divider, RC, capacitance đầu vào ADC và ADC clock, sau đó xác nhận bằng đo.

Điều kiện nghiệm thu:

- sai số settling trong cửa sổ yêu cầu nhỏ hơn budget đã chọn;
- thứ tự bốn channel không gây memory effect/crosstalk đáng kể;
- nếu source impedance cao, tăng sampling time hoặc thêm buffer analog;
- ADC clock nằm trong giới hạn datasheet;
- VDDA/VREF sạch và được decouple đúng; giá trị điện áp quy đổi dùng VDDA đã đo hoặc reference đã hiệu chuẩn.

### 7.4. Double buffering và ISR

- DMA circular phải có half-transfer/full-transfer hoặc cơ chế ping-pong tương đương.
- ISR chỉ clear flag, chụp lỗi tối thiểu và notify task; không printf, không float filter, không gọi vehicle-control đầy đủ.
- Buffer đang được DMA ghi không được task đọc như buffer ổn định.
- Mất notification không được làm dữ liệu cũ trông như dữ liệu mới.
- DMA transfer error, ADC overrun hoặc generation đứng yên phải tạo fault dữ liệu.

### 7.5. Đường bảo vệ nhanh

Bảo vệ được phân lớp:

1. **Lớp phần cứng**: current limit/thermal/short-circuit nội BTS, cầu chì và giới hạn phần cứng khác; đây là tuyến cuối bắt buộc.
2. **ADC analog watchdog hoặc threshold raw tương đương**: có thể cắt emergency nếu topology cho phép một threshold raw bảo thủ cho mọi kênh.
3. **Task current-control 1 kHz**: soft limit, I²t, sensor validity và controlled stop.

Nếu dùng analog watchdog chung cho bốn kênh, threshold phải được chọn theo kênh nhạy nhất và phải tính đến fault-current của IS. Không được dùng analog watchdog làm bảo vệ duy nhất.

### 7.6. Cửa sổ sample hợp lệ đối với BTS IS

Với BTS IS, raw sample chỉ được coi là current sample hợp lệ khi:

- đúng half-bridge theo chiều hiện tại đang ở trạng thái high-side active;
- sample nằm ngoài guard time quanh cạnh PWM;
- INH đang enable;
- duty đủ lớn để có cửa sổ sense;
- tín hiệu chưa ở fault/rail;
- direction state không đang chuyển tiếp/phanh low-side.

Sample ở pha freewheel hoặc brake không được đưa vào trung bình như dòng bằng 0; phải đánh dấu **invalid/not-observable**. Nếu thuật toán dùng duty-weighted average thay vì gating, công thức bù duty và giới hạn duty tối thiểu phải được chứng minh bằng phép đo.

---

## 8. Hợp đồng chuyển đổi, hiệu chuẩn và lọc

### 8.1. Chuyển đổi raw

Quy đổi tổng quát:

\[
V_{ADC}=\frac{Code}{4095}V_{REF,actual}
\]

Với BTS IS lý tưởng:

\[
I_L=\frac{(V_{ADC}-V_{offset})k_{ILIS}}{R_{IS}G_{analog}}
\]

Trong thực tế, phiên bản đầu nên dùng hệ số affine đã đo cho từng motor và chiều:

\[
I_{m,d}=a_{m,d}\,Code+b_{m,d}
\]

hoặc LUT nhiều đoạn nếu sai số phi tuyến đáng kể.

### 8.2. Ma trận hiệu chuẩn bắt buộc

Phải có tối thiểu tám profile:

| Motor | Tiến/A | Lùi/B |
|---|---|---|
| M1 | gain, offset, valid range | gain, offset, valid range |
| M2 | gain, offset, valid range | gain, offset, valid range |
| M3 | gain, offset, valid range | gain, offset, valid range |
| M4 | gain, offset, valid range | gain, offset, valid range |

Mỗi profile phải ghi:

- serial/version board;
- ngày hiệu chuẩn;
- thiết bị chuẩn;
- VDDA;
- nhiệt độ gần đúng;
- các điểm dòng đã đo;
- sai số cực đại trong vùng valid;
- ngưỡng dưới mà tín hiệu không đủ tin cậy;
- raw fault/rail quan sát được.

Không được dùng `k_ILIS=8500` danh định làm calibration sản phẩm cuối. Datasheet cho dải sai số rộng; nó chỉ được dùng để tính sơ bộ và kiểm tra tính hợp lý.

### 8.3. Quy trình zero/offset

- Offset phải được đo khi bridge ở trạng thái đã định nghĩa, motor không mang dòng và nguồn công suất vẫn ở trạng thái đại diện.
- Không tự zero trong lúc xe có thể bị back-drive hoặc đang phanh.
- Auto-zero chỉ được chạy khi command bằng 0 đủ lâu, INH ở trạng thái phù hợp, raw ổn định và không có fault.
- Offset update phải bị giới hạn tốc độ và biên để một sensor fault không bị “học” thành zero mới.

### 8.4. Ba loại tín hiệu dòng

Mỗi motor phải có ba đại lượng khác nhau, không dùng chung một filter cho mọi mục đích:

1. **I_fast/peak**: latency thấp, dùng trip/fast derate; lọc rất ít và có deglitch hữu hạn.
2. **I_control/avg**: lọc low-pass đủ nhanh cho vòng 1 kHz; dùng soft limiter và ramp adaptation.
3. **I_rms/thermal**: cửa sổ dài hơn hoặc I²t; dùng tải nhiệt.

Mọi filter phải:

- dùng `dt` thực hoặc chu kỳ cố định đã kiểm chứng;
- định nghĩa startup behavior;
- không biến sample invalid thành 0;
- saturate an toàn, tránh overflow;
- có unit test step, impulse, noise, missing data và wrap timestamp.

### 8.5. Chất lượng và validity

Snapshot mỗi motor tối thiểu phải diễn đạt được:

- `VALID`;
- `NOT_OBSERVABLE_PWM_WINDOW`;
- `STALE`;
- `ADC_RAIL_LOW`;
- `ADC_RAIL_HIGH`;
- `FAULT_LEVEL_OR_OVERCURRENT`;
- `CALIBRATION_MISSING`;
- `DMA/SEQUENCE_ERROR`;
- `DIRECTION_TRANSITION`;
- `BRAKE_CURRENT_UNOBSERVABLE` nếu dùng BTS IS.

Controller không được dùng một số dòng nếu validity không cho phép. Cờ validity là một phần của API, không phải log phụ.

---

## 9. Hợp đồng command shaping để chuyển động mượt

### 9.1. Hai tầng giới hạn

Chuyển động mượt phải dùng cả:

1. giới hạn slope: acceleration/deceleration tối đa;
2. giới hạn jerk: tốc độ thay đổi acceleration tối đa.

Chỉ moving average dòng không làm lệnh duty hết giật.

Mỗi mode phải có bộ tham số riêng:

| Mode | Accel | Decel | Jerk | Duty cap | Ghi chú |
|---|---:|---:|---:|---:|---|
| Normal/straight | TBD | TBD | TBD | 100% hoặc system cap | tune mặt phẳng trước |
| Arc turn | TBD | TBD | TBD | system cap | giữ tỷ lệ trái/phải |
| Pivot | thấp hơn normal | phù hợp chống giật | thấp | hiện baseline 35% | ma sát scrub cao |
| Reversal transition | ramp về 0 | riêng | riêng | 0 trước đổi chiều | bắt buộc qua zero/deadtime |
| Controlled stop | ưu tiên an toàn | riêng | có thể bỏ jerk nếu cần | giảm về 0 | timeout/sensor degraded |
| Emergency stop | không áp dụng smoothing | không áp dụng | không áp dụng | 0 ngay | cắt INH/CCR an toàn |

### 9.2. Shaping theo vector trái–phải

Không được ramp hai bên một cách gây đổi tỷ lệ tùy ý. Với lệnh rẽ/pivot ổn định, shaper nên thay đổi một scalar magnitude chung theo hướng vector lệnh. Khi người lái đổi curvature, phải giới hạn tốc độ thay đổi cả magnitude và curvature.

Mục tiêu:

- không có bước duty lớn do frame 20–50 Hz;
- không có đổi dấu trực tiếp;
- không có một bên đạt target sớm hơn nhiều chỉ vì rounding;
- pivot vào/ra đối xứng cả bốn motor.

### 9.3. Đổi chiều và phanh

Trình tự bắt buộc:

1. phát hiện target đổi dấu;
2. ramp effort cũ về 0;
3. xác nhận state logic về 0;
4. thực hiện brake/deadtime theo policy;
5. chuẩn bị inactive PWM = 0;
6. chuyển direction;
7. ramp từ 0 theo chiều mới.

Không được dùng “dòng BTS IS bằng 0” làm bằng chứng rotor/xe đã đứng yên, vì IS có thể bằng 0 trong freewheel, brake low-side hoặc reverse current. Current có thể là điều kiện phụ, không phải điều kiện xác nhận duy nhất.

---

## 10. Hợp đồng Current Envelope Governor

### 10.1. Quyền hạn

Governor được phép:

- giảm duty/magnitude;
- giảm acceleration/jerk khi current headroom nhỏ;
- yêu cầu controlled stop hoặc fault;
- giữ duty hiện tại trong biên an toàn khi lệnh tăng quá nhanh.

Governor không được:

- tăng duty cao hơn output của command shaper;
- đổi dấu lệnh;
- tự đảo chiều;
- tự thay đổi tỷ lệ trái–phải ngoài policy đã duyệt;
- che fault để “cố vượt địa hình”.

### 10.2. Soft current limiter

Mỗi motor sinh một hệ số khả dụng `g_i` trong 0..1 từ sai lệch quá dòng mềm. Controller có thể là P/PI giảm một chiều, nhưng phải có:

- output clamp;
- anti-windup;
- release ramp/hysteresis;
- không phản ứng với sample invalid;
- không tăng trên 1;
- parameter theo profile/mode nhưng không vượt system safe current.

Kết hợp mặc định:

\[
g_R=\min(g_{M1},g_{M2}),\quad g_L=\min(g_{M3},g_{M4})
\]

\[
g_{common}=\min(g_R,g_L)
\]

Áp dụng `g_common` cho cả hai bên để giữ curvature. Đây là policy an toàn phiên bản đầu.

### 10.3. Load-adaptive ramp

Khi `I_control` tiến gần `I_soft`, positive acceleration và jerk phải giảm dần. Deceleration không được bị current governor ngăn cản khi cần giảm tải.

Hành vi yêu cầu:

- tải nhẹ: theo profile ramp bình thường;
- tải tăng: tăng duty chậm hơn nhưng vẫn tiến đến lệnh trong biên dòng;
- chạm soft limit: governor giảm/giữ effort;
- vượt hard/thermal limit: chuyển state an toàn.

Đây là cơ chế chính giúp vượt địa hình “có kiểm soát”: duy trì torque trong envelope thay vì giật duty hoặc cắt bật liên tục.

### 10.4. Cân bằng motor cùng bên

Phiên bản đầu **không ép dòng hai motor cùng bên bằng nhau**.

Chênh dòng M1–M2 hoặc M3–M4 được dùng để:

- chẩn đoán tải không đều;
- phát hiện một motor nghi kẹt/đứt dây/cảm biến lỗi;
- chọn constraint nghiêm nhất của bên.

Một trim bù tải trước/sau chỉ được mở ở phase nâng cao nếu:

- tổng effort của bên được bảo toàn;
- biên trim nhỏ, ví dụ giới hạn được tune và ghi rõ;
- trim bị disable khi pivot, low duty, reversal, sensor invalid hoặc transient;
- thử nghiệm chứng minh không làm bánh nhẹ trượt;
- tốt nhất đã có encoder/IMU.

### 10.5. Nghi kẹt

Vì không có encoder, chỉ được đặt tên trạng thái là `STALL_SUSPECTED`, không phải `STALL_CONFIRMED`.

Điều kiện tối thiểu:

- command/duty lớn hơn ngưỡng quan sát;
- dòng cao liên tục hơn thời gian xác định;
- không ở start transient, pivot transition hoặc brake;
- sensor valid;
- có hysteresis.

Phản ứng mặc định:

1. derate;
2. nếu kéo dài, controlled stop toàn xe;
3. latch nếu lặp lại quá số lần cho phép;
4. không tự reverse/retry trừ khi sau này có yêu cầu và đánh giá an toàn riêng.

---

## 11. State machine và fault policy bắt buộc

### 11.1. State cấp hệ thống

| State | Cho phép PWM | Ý nghĩa |
|---|---|---|
| INIT_SAFE | Không | khởi động, CCR=0, INH disable |
| SENSOR_QUALIFY | Không hoặc commissioning cap rất thấp | xác nhận ADC/DMA/calibration |
| READY | Không | đủ điều kiện, chờ lệnh valid |
| DRIVE | Có | vận hành bình thường |
| DERATE | Có, bị giảm | soft current/I²t/load constraint |
| CONTROLLED_STOP | giảm về 0 | timeout hoặc lỗi không tức thời |
| EMERGENCY_STOP | Không | cắt ngay |
| FAULT_LATCHED | Không | chờ quy trình reset |

### 11.2. Priority fault

Thứ tự ưu tiên từ cao xuống thấp:

1. hard overcurrent/fault flag/ADC overrange nguy hiểm;
2. bridge/motor thermal budget vượt giới hạn;
3. ADC/DMA corruption hoặc current snapshot stale trong khi đang drive;
4. command brake;
5. command timeout/invalid;
6. stall suspected kéo dài;
7. soft current limit;
8. normal drive.

Fault mức cao không được bị state mức thấp ghi đè trong cùng cycle.

### 11.3. Phạm vi dừng khi một motor lỗi

Policy mặc định phiên bản đầu: một motor hard fault làm **emergency stop toàn xe**.

Lý do: dừng riêng một motor trong xe skid-steer 4 bánh có thể gây yaw mạnh, kéo quá tải motor còn lại cùng bên hoặc gây mất kiểm soát. Chỉ được chuyển sang dừng riêng từng motor khi có phân tích an toàn cơ khí và thử nghiệm riêng.

### 11.4. Reset fault

Fault latched chỉ được clear khi:

- command đã ở zero/brake;
- nguyên nhân không còn;
- current snapshot valid trở lại nếu liên quan sensor;
- đã qua cooldown/reset time;
- có explicit reset hợp lệ hoặc power-cycle theo policy;
- không tự động resume theo command cũ.

Sau reset phải trở lại READY, không nhảy trực tiếp DRIVE.

### 11.5. Sensor failure

Khi current feedback đã tham gia bảo vệ, sensor invalid kéo dài không được coi như dòng bằng 0.

- rail-high/fault-level: emergency stop;
- DMA/ADC sequence error: emergency hoặc controlled stop theo mức độ và latency đã phê duyệt;
- stale ngắn: giữ output không tăng trong grace period rất ngắn;
- stale quá giới hạn: controlled stop hoặc emergency theo hazard analysis;
- calibration missing: không cho DRIVE production;
- not-observable do duty quá thấp: cho phép chỉ khi nằm trong vùng đã định nghĩa và không dùng sample đó để update limiter.

---

## 12. Scheduling, concurrency và ownership

### 12.1. Chu kỳ điều khiển

Mục tiêu mặc định:

- PWM carrier: 14 kHz;
- acquisition: 56–112 k complete scan/s trong giai đoạn khảo sát;
- current processing + safety/control update: 1 kHz định kỳ;
- app command: hiện dự kiến 20–50 Hz, timeout 100 ms;
- telemetry thường: 10–50 Hz;
- raw capture: chỉ bật có thời hạn khi commissioning.

Control loop phải dùng periodic scheduling kiểu `delay-until` hoặc wake theo DMA/timer với deadline đo được. `osDelay(1)` sau một chuỗi việc có thời gian thay đổi không được coi là chu kỳ 1 kHz chính xác.

### 12.2. Task bắt buộc tách

Kiến trúc production tối thiểu cần phân tách logic:

- `Control/Safety task`: priority cao, chu kỳ 1 kHz, bounded execution;
- `Communication task`: giải mã frame/ACK;
- `Telemetry task`: priority thấp, non-blocking;
- acquisition interrupt/DMA notification.

Có thể gộp current processing vào Control task nếu WCET đạt; không được gộp log blocking vào đó.

### 12.3. Deadline và age

Mỗi cycle phải biết:

- current snapshot age;
- command age;
- actual `dt`;
- deadline miss counter;
- DMA generation tiến triển.

Giá trị khởi đầu để nghiệm thu timing:

- control nominal 1,000 µs;
- current snapshot không quá 2 control cycles khi DRIVE;
- một deadline miss đơn không được tạo bước duty lớn;
- miss liên tiếp vượt ngưỡng phải controlled stop.

Con số cuối phải được xác nhận bằng trace/GPIO timing và ghi vào parameter sheet.

### 12.4. Atomic snapshot

Snapshot giữa ISR/task và giữa task phải dùng double-buffer, critical section rất ngắn hoặc cơ chế atomic generation phù hợp. Không được đọc một struct đang bị ghi dở. `volatile` đơn thuần không được coi là cơ chế đồng bộ hoàn chỉnh.

---

## 13. Parameter contract

### 13.1. Nguồn gốc tham số

Mọi tham số phải thuộc một trong bốn nguồn:

- datasheet có dẫn nguồn;
- tính từ phần cứng đã biết;
- đo/fit từ test;
- yêu cầu trải nghiệm được tune nhưng nằm dưới giới hạn an toàn.

Không chấp nhận “magic number” không đơn vị/không lý do.

### 13.2. Danh sách tham số bắt buộc

| Nhóm | Tham số |
|---|---|
| Analog | VDDA, R_IS/divider/gain, sample time, RC, rail thresholds |
| Calibration | gain/offset/LUT × 4 motor × 2 chiều, valid min/max |
| Timing | PWM, ADC scan, buffer length, control period, freshness |
| Current | I_cont, I_soft, I_peak, I_trip, I_release, debounce |
| Thermal | I²t capacity, cooling decay, cooldown |
| Motion | accel/decel/jerk theo mode, pivot cap, reverse deadtime |
| Governor | Kp/Ki hoặc law tương đương, anti-windup, release rate |
| Plausibility | rail low/high, stuck-low/high time, min observable duty |
| Safety | timeout, max deadline misses, latch/reset conditions |

### 13.3. Cách chọn giới hạn dòng

Dòng an toàn hệ thống phải lấy theo phần tử yếu nhất:

\[
I_{system}(t)=\min(I_{motor}(t),I_{bridgePCB}(t),I_{connector}(t),I_{wire}(t),I_{battery}(t),I_{fuse}(t),I_{sensorRange}(t))
\]

Sau đó áp dụng margin cho nhiệt độ, sai số cảm biến và sai số calibration. Con số “current limitation 43 A typical” của BTS7960 không phải permission cho hệ thống chạy 43 A.

### 13.4. Version và traceability

Parameter set phải có version/hash và được in trong startup telemetry. Log test phải ghi parameter version. Thay tham số an toàn phải làm invalid kết quả test cũ liên quan.

---

## 14. Telemetry và khả năng chẩn đoán

### 14.1. Trường bắt buộc

Mỗi record decimated cần đủ các trường:

- timestamp và sequence command;
- command left/right raw;
- mode motion;
- shaped left/right;
- duty requested và duty applied M1..M4;
- raw ADC gần nhất hoặc thống kê raw;
- I_fast, I_control, I_rms/I²t M1..M4;
- validity M1..M4;
- `g_i`, `g_side`, `g_common`;
- state system và per-motor;
- fault bitmap + first-fault timestamp;
- command age, current age, deadline miss, DMA overrun.

### 14.2. Nguyên tắc log

- Không format chuỗi trong ISR.
- Không log mỗi ADC sample trong vận hành thường.
- Raw capture dùng buffer nhị phân có giới hạn thời gian.
- Khi queue đầy, bỏ telemetry có counter; không chặn control.
- First-fault context phải được giữ, không bị fault phụ ghi đè.

---

## 15. Kế hoạch thực hiện bắt buộc theo gate

> Phần này chỉ mô tả quá trình tương lai. Tài liệu hiện tại chưa thực hiện bất kỳ thay đổi code nào.

### G0 — Chốt phần cứng và hazard

**Công việc**

1. Điền toàn bộ bảng HW-01..HW-12.
2. Vẽ lại signal path từ IS/sensor đến PA0/PA1/PC0/PC1.
3. Tính worst-case voltage ADC normal/fault.
4. Chốt dòng an toàn theo motor/PCB/wire/connector/pin/fuse.
5. Xác nhận cảm biến có quan sát được cả hai chiều và các pha nào.
6. Chốt bảng mapping motor–ADC.

**Deliverable**

- schematic/ảnh đo;
- bảng giới hạn;
- mapping ký xác nhận;
- parameter sheet bản đầu.

**Gate pass**

- không còn TBD có thể làm hỏng MCU/motor;
- điện áp ADC được chứng minh an toàn;
- bi-direction visibility hoặc giới hạn chức năng được phê duyệt rõ.

### G1 — Acquisition-only, motor bị disable

**Thay đổi dự kiến**

- cập nhật `.ioc` cho scan 4 rank, trigger và DMA;
- tạo module acquisition/monitor riêng;
- thêm DMA/ADC IRQ handling bounded;
- thêm telemetry raw test;
- tuyệt đối giữ INH disable khi test nguồn analog.

**Kiểm thử**

- inject bốn mức điện áp từng kênh;
- xác minh rank order, scaling, half/full buffer;
- đo sample timing, crosstalk, noise, overrun;
- test disconnect/short GND/short mức clamp hợp lệ.

**Gate pass**

- mapping 100% đúng;
- không DMA overrun trong soak test;
- noise/settling trong budget;
- fault ADC/DMA tạo state đúng khi motor vẫn disable.

### G2 — Shadow mode, chưa có quyền sửa duty

**Công việc**

- chạy xe trên giá kê với controller hiện tại;
- current subsystem chỉ quan sát và log;
- hiệu chuẩn 8 profile motor × chiều bằng ammeter/current probe chuẩn;
- xác định valid PWM window và min observable duty;
- đối chiếu fault output BTS.

**Gate pass**

- sai số đạt budget trong declared valid range;
- direction A/B đều quan sát đúng hoặc limitation đã xử lý phần cứng;
- không ảnh hưởng timing control hiện tại;
- current validity không báo giả quá ngưỡng cho phép.

### G3 — Bật bảo vệ, chưa bật soft control

**Công việc**

- bật rail/fault/overcurrent/I²t detection;
- bật state Safety Arbiter;
- fault chỉ cắt/stop, chưa tune governor;
- fault injection từng kênh.

**Gate pass**

- mọi fault phản ứng đúng phạm vi và latency;
- reset không auto-resume;
- first-fault log đầy đủ;
- không nuisance trip trong profile hợp lệ.

### G4 — Bật command shaping hoàn chỉnh

**Công việc**

- chuyển ramp sang scheduler định kỳ;
- thêm jerk limit;
- giữ state machine reversal/pivot đối xứng;
- unit test toàn bộ transition.

**Gate pass**

- không bước duty ngoài budget;
- mọi đổi dấu đi qua zero/deadtime;
- curvature command được giữ trong tolerance khi ramp;
- timeout/brake/fault vẫn có priority đúng.

### G5 — Bật soft current governor

**Công việc**

- bật `g_i`, `g_side`, `g_common`;
- bật load-adaptive acceleration;
- tune bằng tải có kiểm soát;
- chứng minh governor chỉ giảm, không boost.

**Gate pass**

- dòng giữ trong envelope đã định;
- không hunting/cắt-bật;
- khi một motor chạm limit, xe giảm biên độ có phối hợp;
- tỷ lệ left/right không đổi ngoài tolerance trừ fault.

### G6 — Test xe trên mặt đất và địa hình

Thứ tự bắt buộc:

1. mặt phẳng, tốc độ thấp;
2. thẳng tiến/lùi;
3. arc trái/phải;
4. pivot hai chiều;
5. chuyển normal↔pivot;
6. dốc/tải tăng dần;
7. địa hình lệch tải;
8. tình huống bánh bị cản có kiểm soát;
9. soak nhiệt.

Mỗi test phải có người giữ emergency power cut, vùng trống và giới hạn duty ban đầu.

### G7 — Release

Chỉ release khi:

- toàn bộ acceptance matrix đạt;
- parameter version được đóng băng;
- build/test tái lập được;
- tài liệu mapping và quy trình reset fault cập nhật;
- không còn log debug blocking;
- rollback firmware cũ được lưu;
- người sở hữu xe ký chấp thuận limitation current-only.

---

## 16. Ma trận kiểm thử và tiêu chí nghiệm thu

### 16.1. Unit test host

Bắt buộc có test cho:

- raw→current tại min/mid/max và saturation;
- calibration riêng 8 profile;
- invalid sample không bị đưa thành zero;
- fast/avg/RMS/I²t filter;
- soft limiter clamp + anti-windup + release;
- common-scale bảo toàn tỷ lệ trái/phải;
- mode normal/arc/pivot/reversal/stop;
- mọi transition state machine;
- current stale, DMA error, rail high/low;
- timestamp wrap, command timeout, deadline miss;
- fault priority và latch/reset;
- null/invalid input và boundary integer overflow.

### 16.2. Hardware-in-the-loop/bench

| Test | Điều kiện đạt |
|---|---|
| ADC injection | từng kênh đúng index, sai số trong budget |
| PWM/ADC timing | waveform chứng minh trigger/coverage/guard time |
| Hai chiều | mỗi motor A/B đều có signal theo declared behavior |
| Freewheel/brake | validity phản ánh đúng vùng không quan sát được |
| Fault level | không vượt điện áp ADC; safety state đúng |
| DMA soak | không overrun/corruption trong thời gian test đã chốt |
| CPU/WCET | control deadline đạt ở worst logging/communication load |
| EMI/noise | không false trip khi bốn motor switching |
| Power cycle | luôn boot với INH disable và không auto-run |

### 16.3. Test động lực học

Nếu chưa có encoder/IMU, tiêu chí chính thức chỉ được dựa trên duty/current/timing và quan sát đo ngoài. Không được ghi “speed error” nếu không có thiết bị đo tốc độ độc lập.

Tối thiểu phải đo:

- duty step lớn nhất;
- acceleration/jerk command thực;
- peak/average/RMS current;
- thời gian vào limit và hồi phục;
- số lần nuisance fault;
- độ lệch tỷ lệ left/right do limiter;
- nhiệt motor/driver/connector sau soak;
- thời gian stop khi timeout/fault.

Các target số cụ thể về current accuracy, latency và smoothness phải được điền sau G0/G2. Không được tự đặt một con số đẹp nhưng không đo được.

### 16.4. Fault injection bắt buộc

- rút từng sensor;
- short sensor xuống GND;
- đưa sensor lên ngưỡng clamp an toàn;
- dừng DMA/gây stale bằng test hook;
- sai rank/order bằng test build;
- command timeout;
- brake khi đang chạy;
- đổi dấu ở duty cao;
- cản một bánh/từng motor có giới hạn cơ khí;
- fault trong pivot;
- reset trong khi command vẫn khác 0;
- queue telemetry đầy;
- UART load cao đồng thời control chạy.

---

## 17. Danh sách file dự kiến khi bước vào triển khai

Danh sách này là thiết kế dự kiến, **không phải thay đổi đã thực hiện**:

| File/module | Vai trò dự kiến |
|---|---|
| `external_hbridge_f407.ioc` | scan 4 ADC rank, trigger, DMA, IRQ |
| `Core/Inc/current_sense.h` | API snapshot/validity/calibration |
| `Core/Src/current_sense.c` | acquisition processing, filter, monitor |
| `Core/Inc/safety_supervisor.h` | state/fault contract |
| `Core/Src/safety_supervisor.c` | arbiter, latch/reset, I²t |
| `Core/Src/vehicle_control.c` | command shaper + current governor integration |
| `Core/Src/motor_bts.c` | actuator ownership/emergency-disable/query applied state |
| `Core/Src/freertos.c` | tách task/control scheduling |
| `Core/Src/stm32f4xx_it.c` | DMA/ADC IRQ bounded |
| `Core/Src/usart6_log.c` | telemetry decimated/fault context |
| `CMakeLists.txt` | đăng ký module/test mới |
| `tests/test_current_sense.c` | conversion/filter/validity |
| `tests/test_safety_supervisor.c` | fault/state/timeout |
| `tests/test_vehicle_control.c` | governor + jerk + pivot/reversal |

Quy tắc CubeMX:

- sửa `.ioc` trước, regenerate có kiểm soát;
- bảo toàn USER CODE section;
- diff generated code trước khi nhận;
- không để cấu hình thật chỉ tồn tại trong `.c` mà `.ioc` không phản ánh;
- cập nhật tài liệu pin nếu regenerate làm thay đổi mapping.

---

## 18. Điều kiện để nâng cấp lên điều khiển kín đầy đủ

Khi bổ sung encoder và IMU, kiến trúc có thể mở rộng thành cascade:

1. outer motion/yaw loop tạo `v_L*`, `v_R*`;
2. speed PI cho từng bên hoặc từng bánh;
3. current/torque envelope vẫn là constraint bên trong;
4. traction logic dùng chênh wheel speed và vehicle/yaw estimate;
5. current dùng xác nhận tải và giới hạn mô-men.

Lúc đó mới có thể cam kết:

- giữ tốc độ theo tải;
- đi thẳng theo yaw;
- xác nhận wheel stall/slip tốt hơn;
- torque redistribution có cơ sở quan sát.

Phần current subsystem của hợp đồng này phải được thiết kế sao cho có thể tái sử dụng trong kiến trúc cascade đó.

---

## 19. Tài liệu chuẩn tham chiếu

1. Infineon, **BTS 7960 High Current PN Half Bridge Datasheet**, đặc biệt mục 4.4.4, 4.4.5, 4.4.6 và 6.2:  
   <https://www.infineon.com/assets/row/public/documents/10/57/infineon-bts7960-ds-en.pdf>
2. STMicroelectronics, **RM0090 — STM32F405/407/415/417 Reference Manual**, phần ADC, DMA và timer:  
   <https://www.st.com/resource/en/reference_manual/rm0090-stm32f407-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
3. STMicroelectronics, **AN2834 — How to optimize the ADC accuracy in the STM32 MCUs**:  
   <https://www.st.com/resource/en/application_note/an2834-how-to-optimize-the-adc-accuracy-in-the-stm32-mcus-stmicroelectronics.pdf>
4. Các nguồn nội bộ phải đọc cùng tài liệu này:
   - `external_hbridge_f407.ioc`;
   - `Core/Src/adc.c`, `dma.c`, `tim.c`, `freertos.c`;
   - `Core/Src/motor_bts.c`, `differential_control.c`, `vehicle_control.c`;
   - các test hiện có trong `tests/`.

Nếu datasheet module thực tế khác BTS7960 chính hãng hoặc board có mạch biến đổi IS riêng, tài liệu của module/mạch đó phải được thêm vào đây trước G0 pass.

---

## 20. Biên bản chấp thuận trước khi code

### 20.1. Các quyết định đã được hợp đồng chọn

- [x] Dùng current feedback làm torque/load envelope, không giả làm speed feedback.
- [x] Giữ lệnh app dạng left/right, không trộn vi sai lần hai.
- [x] Dùng jerk-limited command shaping để tạo độ mượt.
- [x] Dùng common scaling để giữ tỷ lệ trái/phải khi soft limit.
- [x] Không ép bốn motor cùng dòng.
- [x] Một hard fault motor mặc định dừng toàn xe.
- [x] Không auto-boost vượt lệnh và không auto-reverse thoát kẹt.
- [x] Triển khai tuần tự acquisition → shadow → protection → shaping → governor.

### 20.2. Các mục phải được chủ xe cung cấp/xác nhận trước G0 pass

- [ ] Sơ đồ hoặc ảnh đấu bốn đường dòng.
- [ ] Mapping PA0/PA1/PC0/PC1 ↔ M1/M2/M3/M4.
- [ ] Topology hai IS của mỗi cầu H.
- [ ] Giá trị R_IS/divider/filter/clamp.
- [ ] Thông số motor và dòng cho phép của toàn đường công suất.
- [ ] Điện áp pin và cầu chì.
- [ ] Chiều quay/vị trí vật lý từng motor.
- [ ] Mức ưu tiên giữa bảo vệ, khả năng vượt địa hình và cảm giác lái.

### 20.3. Chữ ký/phiên bản

| Vai trò | Tên | Ngày | Phiên bản chấp thuận |
|---|---|---|---|
| Chủ sở hữu/yêu cầu |  |  |  |
| Người triển khai firmware |  |  |  |
| Người xác nhận phần cứng |  |  |  |

Mọi thay đổi sau khi ký phải thêm changelog: nội dung đổi, lý do, ảnh hưởng safety, test cần chạy lại và phiên bản parameter mới.

---

## 21. Kết luận ràng buộc cuối cùng

Kiến trúc này chỉ được coi là hoàn thành khi nó vừa đáp ứng cảm giác lái vừa chứng minh được an toàn bằng số liệu. Dòng phản hồi sẽ giúp xe giới hạn mô-men có kiểm soát, tránh vọt dòng, phối hợp giảm công suất giữa hai bên và nhận biết tải bất thường; nó không thể thay thế encoder/IMU cho bài toán tốc độ và độ bám.

**Mã acquisition/shadow có thể tồn tại để kiểm thử bench, nhưng không được bật quyền can thiệp duty hoặc coi threshold là bảo vệ an toàn trước khi Gate G0 xác minh phần cứng, mapping và dải điện áp/dòng thực tế.**

---

## 22. Nhật ký triển khai ngày 2026-09-02

### 22.1. Trạng thái triển khai

Đã triển khai phần đọc dòng bốn motor và điểm tích hợp với điều khiển vi sai. Do các mục `HW-01..HW-09` vẫn chưa được xác nhận, hệ thống khởi động ở **acquisition/shadow mode**:

- ADC/DMA vẫn thu raw để kiểm tra bench;
- dữ liệu chưa có mapping/calibration hợp lệ bị gắn cờ invalid, không bị biến thành `0 A`;
- không có ngưỡng dòng mặc định ước đoán;
- governor không thể bật nếu chưa xác nhận mapping, chưa đủ tám profile hiệu chuẩn hoặc chưa đủ bốn current envelope;
- luồng điều khiển cũ vẫn chạy như trước khi governor đang khóa.

Đây là thay đổi có chủ ý so với câu “chưa code” của bản `0.1-draft`: chỉ phần khung G1/G2 và điểm tích hợp G5 được viết trước; quyền tác động công suất vẫn bị chặn bởi điều kiện commissioning.

### 22.2. File đã bổ sung/thay đổi và tác dụng

| File | Nội dung bổ sung/thay đổi | Tác dụng |
|---|---|---|
| `Core/Inc/current.h` | Hợp đồng dữ liệu dòng, tám profile motor × chiều, quality flags, snapshot immutable, current envelope và khóa commissioning | Tách raw/current/validity rõ ràng; controller không dùng nhầm mẫu stale, mẫu lúc phanh hoặc mẫu chưa hiệu chuẩn |
| `Core/Src/current.c` | ADC1 DMA ping-pong, xử lý block 32 scan, fast/control/RMS, phát hiện rail/DMA/overrun/sequence, freshness và publish snapshot | Đọc đồng thời bốn đường dòng không polling blocking; ISR chỉ clear flag + notify task; filter không cập nhật bằng số 0 khi mẫu invalid |
| `Core/Inc/current_governor.h`, `Core/Src/current_governor.c` | Governor giảm một chiều, deglitch hard limit theo generation, xác nhận dữ liệu invalid, attack/release ramp và common scale | Motor chạm soft limit làm cả hai bên cùng giảm theo hệ số nghiêm nhất, nhờ đó giữ tỷ lệ trái–phải; governor không boost và không đổi dấu lệnh |
| `Core/Src/adc.c` | ADC1 scan 4 rank: IN0, IN1, IN10, IN11; DMA unlimited; sampling 56 cycles; trigger TIM8 TRGO | Mỗi DMA frame luôn có thứ tự ứng viên `[M1,M2,M3,M4]`; thời gian sample dài hơn cấu hình 3 cycles cũ và đạt bốn complete scan trên một chu kỳ PWM theo timing danh định |
| `Core/Src/tim.c`, `Core/Inc/tim.h` | Thêm TIM8 TRGO khoảng `168 MHz / 2947 ≈ 57.0 kHz` | Tạo trigger ADC bằng phần cứng, độc lập task 1 ms; tần số không phải bội nguyên chính xác của PWM 14 kHz để quét pha theo phương án oversampling dự phòng |
| `external_hbridge_f407.ioc` | Đồng bộ cấu hình ADC1 4 rank, DMA continuous, TIM8 trigger và sampling time | Giữ `.ioc` là nguồn cấu hình để lần regenerate sau không quay về ADC một kênh |
| `Core/Inc/motor_bts.h`, `Core/Src/motor_bts.c` | Snapshot trạng thái actuator `DRIVE/COAST/BRAKE`, direction/command, transition generation và `MotorBts_EmergencyDisableAll()` | Current task biết khi nào BTS IS quan sát được; mẫu phanh/đổi chiều bị loại; hard fault có primitive cắt toàn bộ INH và CCR idempotent |
| `Core/Src/vehicle_control.c`, `Core/Inc/vehicle_control.h` | Đưa current frame vào vòng điều khiển; áp common scale sau ramp và trước calibration/PWM; latch hard fault | Kết hợp phản hồi dòng với vi sai điện tử mà không trộn vi sai lần hai và không làm lệch tỷ lệ trái–phải; calibration PWM cũng bị chặn không được boost vượt lệnh |
| `Core/Src/freertos.c` | Thêm `currentTask` priority High, stack 1536 byte; default task lấy current frame trước khi gọi vehicle control | Acquisition/processing tách khỏi communication/log; vòng điều khiển chỉ đọc snapshot đã publish |
| `Core/Src/stm32f4xx_it.c` | Nối `DMA2_Stream4_IRQHandler` với ISR bridge của current module | DMA half/full/error đánh thức task dòng mà không chạy filter hay log trong ISR |
| `Core/Src/main.c` | Khởi tạo TIM8 và current subsystem sau khi motor về safe state | ADC trigger chỉ bắt đầu khi current task đã sẵn sàng nhận notification |
| `CMakeLists.txt` | Đăng ký `current.c` và `current_governor.c` | Hai module mới được biên dịch vào firmware |
| `tests/test_current_governor.c` và test stubs | Test common scale, reduction-only, hard-fault deglitch theo generation và controlled stop khi dữ liệu invalid | Chứng minh phần toán governor trên host, độc lập phần cứng |

### 22.3. Luồng chạy sau thay đổi

```text
TIM8 TRGO ~57 kHz
        ↓
ADC1 rank [IN0, IN1, IN10, IN11]
        ↓
DMA2 Stream4 circular: half/full IRQ
        ↓
currentTask: raw → validity → fast/control/RMS → snapshot
        ↓
VehicleControl: left/right → differential mapping → ramp/reversal/pivot
        ↓
CurrentGovernor: min(g_M1..g_M4) = g_common, chỉ giảm
        ↓
motor calibration reduction-only → MotorBts → PWM/INH
```

Tác dụng quan trọng nhất của cách ghép này là khi một motor tiến gần current envelope, cả lệnh trái và phải cùng nhân với `g_common`. Xe giảm effort nhưng không bị một bên tự cắt duty làm thay đổi curvature đột ngột. Dòng vẫn chỉ đại diện effort/tải; thuật toán không dùng dòng để tuyên bố đã điều khiển kín tốc độ hoặc phát hiện slip tuyệt đối.

### 22.4. Khóa an toàn và trình tự mở governor

Giá trị mặc định sau reset:

- `mapping_confirmed = 0`;
- cả tám calibration profile ở trạng thái missing;
- cả bốn soft/hard limit bằng `0` (chưa cấu hình);
- `governor_enabled = 0`.

Sau khi Gate G0/G2 đạt, code commissioning phải thực hiện đủ thứ tự:

1. gọi `Current_SetCalibration()` cho M1..M4 ở cả `FORWARD` và `REVERSE` bằng hệ số affine đã đo;
2. gọi `Current_SetProtectionLimits()` cho từng motor bằng giới hạn của phần tử yếu nhất trong đường công suất;
3. xác minh vật lý thứ tự PA0/PA1/PC0/PC1 rồi gọi `Current_SetMappingConfirmed(1)`;
4. chỉ sau bench test mới gọi `Current_EnableGovernor(1)` và kiểm tra giá trị trả về;
5. nếu bất kỳ điều kiện nào thiếu, API từ chối bật và firmware tiếp tục shadow mode.

Không được điền `kILIS=8500`, `R_IS=1 kΩ`, `12 A/18 A` hoặc mapping tuần tự chỉ vì đó là giá trị “có vẻ hợp lý”. Những giá trị này phải đến từ sơ đồ và phép đo của đúng xe.

### 22.5. Kết quả kiểm tra phần mềm

Đã chạy và đạt:

- host test `differential_control`: mapping và direction;
- host test `vehicle_control`: pivot đối xứng, brake transition, common-scale tích hợp và emergency stop toàn xe khi hard current;
- host test `current_governor`: common-scale, không boost/đổi dấu, hard-fault deglitch và invalid-data controlled stop;
- build firmware ARM Debug hoàn chỉnh, sinh `.elf/.hex/.bin`; RAM dùng `24,224 B / 128 KB`, Flash dùng `46,620 B / 1 MB` tại lần build này.

Các test trên chưa thay thế Gate G0/G1/G2 phần cứng. Trước khi bật governor vẫn phải đo timing TIM8→ADC→DMA, kiểm tra thứ tự rank bằng injection, kiểm tra coverage pha PWM, xác minh sampling 56 cycles phù hợp source impedance, thử hai chiều từng motor và fault-injection theo mục 16.

### 22.6. Phần chưa nằm trong lần triển khai này

- Chưa bật analog watchdog/emergency cut trực tiếp từ ADC vì chưa có raw threshold an toàn chung.
- Chưa chốt I²t/thermal budget vì chưa có thông số motor, dây, bridge, pin và điều kiện làm mát.
- Chưa lập telemetry packet đầy đủ cho current snapshot/governor/fault context.
- Chưa tách `safety_supervisor.c`; hard-current latch hiện được chấp hành tại `VehicleControl` và emergency primitive tại `MotorBts`.
- Chưa chứng minh bằng oscilloscope rằng oversampling khoảng 57 kscan/s bao phủ đủ vùng ON hợp lệ của cả ba PWM timer; vì vậy dữ liệu hiện chỉ được dùng shadow cho đến Gate G2.

Các mục này phải hoàn tất trước G3/G5/G7 tương ứng; việc source đã build không đồng nghĩa xe đã đủ điều kiện chạy tải.
