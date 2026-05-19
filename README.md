# OpenCamera
OpenCamera 📸

An open-source, hybrid mechanical, digitally timed Single-Lens Reflex (SLR) camera system built on the ESP32-C3 microcontroller. OpenCamera bridges the gap between classic analog photography and modern DIY electronics by automating the mirror flip, focal-plane shutter speed measurement, and user interface.

OpenCamera separates its operations into three core subsystems to maximize power efficiency and prevent high-current voltage sags:

The Mirror Loop (Servo-driven): Instantly flips the viewing mirror up (to 75°) out of the optical path before exposure and drops it back down (to 40°) immediately afterward.

The Shutter Sweep (H-Bridge + DC Motor): Drives the rack-and-pinion focal-plane slit across the film gate.

The Sensor Gate (High-speed Photodiode): Tracks physical slit timing down to the microsecond ($\mu s$) level to compute and log the exact shutter speed.

Pin Mapping (ESP32-C3 DevKit)

By removing the PCF8575 expander and stepper systems, the pinout has been simplified to direct digital input connections:

Component

Pin Name

GPIO

Type

Description

H-Bridge

PWMA_PIN

10

Output

Shutter motor speed control (PWM)

H-Bridge

AIN1

20

Output

Shutter motor direction control

H-Bridge

AIN2

21

Output

Shutter motor direction control

H-Bridge

STBY

0

Output

H-Bridge standby gate (Active HIGH)

IR Sensor

SENSOR_PIN

3

Input

Photodiode/Photogate slit detector

Trigger

TRIGGER_BTN

1

Input

Physical shutter button (Internal Pullup)

OLED Screen

SDA

8

I2C

System status display

OLED Screen

SCL

9

I2C

System status display

Mirror Servo

SERVO_PIN

7

Output

PWM Mirror Servo (SG90)

UI Button

SPEED_UP_BTN

4

Input

Speed increment button (Internal Pullup)

UI Button

SPEED_DOWN_BTN

5

Input

Speed decrement button (Internal Pullup)

⏱️ Shutter Dynamics & Calculations

Shutter speed measurement relies on the physical dimensions of the shutter slit passing across the photodetector:

$$\text{Velocity } (v) = \frac{\text{Gate Distance } (D_{\text{gate}})}{t_2 - t_1}$$

$$\text{Shutter Speed } (T) = \frac{\text{Slit Width } (W_{\text{slit}})}{v} \times 1000 \text{ ms}$$

Dynamic Jam Protection (Adaptive Timeout)

To protect your SLA-printed gears and H-bridge silicon from melting during a mechanical jam, the firmware features a Dynamic Scaling Timeout. The time-out window scales inversely with the PWM speed:

At Low PWM (0), the timeout is capped at 200ms.

At High PWM (255), the timeout scales down to 25ms.

# Getting Started

Prerequisites

You need the following libraries installed in your Arduino IDE / PlatformIO environment:

Adafruit SSD1306 (OLED UI display)

Adafruit GFX Library (Text rendering)

ESP32Servo (Required for hardware timer allocation on the C3 architecture)

Calibration

Measure the exact spacing of your physical gate distance (default 16.0 mm).

Measure your focal-plane slit width (default 2.0 mm).

Update these lines in your sketch to calibrate exposure calculations:

const float GATE_DISTANCE  = 16.0; 
const float SLIT_WIDTH     = 2.0;


# Serial Telemetry Interface

OpenCamera outputs machine-readable data streams alongside human-readable debugging logs. This allows you to graph exposure speeds in real-time or connect a companion dashboard:

Shutter Timing Output: SHUTTER:8.4500 (value in ms)

System State: SYS_STATE:IDLE, SYS_STATE:MIRROR UP, SYS_STATE:RUNNING

Error Warning Log: ERROR: Shutter stuck over IR beam sensor (Timeout: 45ms).

# Contributing

We welcome structural improvements, especially around mechanical CAD enclosure designs, lens mounts, and physical shutter carriage geometry. Submit a PR or open an issue!
