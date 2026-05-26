# Vostok micro-Kernel
This is a micro-operating system kernel written for the 8-bit AVR architecture (Arduino Uno).

## Introduction 
This project started as a kernel for ATmega32A and was later ported to Arduino Uno R3 (ATmega328).  
Due to limited hardware and unstable ISP programmer, development continues mainly on Arduino Uno.  
The project stems from my love for Unix-based computers and embedded devices.  
Although not as advanced as professional embedded OS, it is a hobby project — come and see for yourself!

## Features available on the Kernel
- Core Kernel: The kernel is written entirely in the C language.
- Preemtive Multitasking: Supports prioritized multitasking with Context Switch (port from ATmega32A).
- VFS: Equip the basic nodes for communication and data transmission: uart0, pipe0, i2c0, button0 (more may be added later).
- Memory Management: Although written for devices without an MMU, it can be managed manually via PROC_STACK_SZ and MAX_PROC. Since the Arduino Uno using the ATmega328 with only 2KB of SRAM, I added       PROGMEM for functions using UART (because they are extremely RAM-intensive when you want to spam the kernel).
- Driver Device: The kernel already includes functions and command combinations to control devices such as UART, I2C, Servo (this cannot yet be extended to all PWM pins that Arduino has), and Button      (this only has pin 2; I've written this code so it might be useful later).
- POSIX-like: I added properties and POSIX-like pseudo-proprietaries including the functions "k_sys_fork()" (the actual fork() function), "k_sys_open()" (which is the actual open() function), ... and     countless similar functions.
- Scheduler: Implements a Preemptive Priority-based Scheduler. It utilizes a system timer interrupt to trigger context switches, ensuring that high-priority tasks are executed promptly while              maintaining fairness through a round-robin mechanism for tasks of equal priority.
- main.c : The avrmain.c file is where tasks can be run and initialized. Specifically, in the main.c function (in the int main() function), I've added functions to load the kernel, scheduler, and         drivers (Warning: I advise against modifying them as some components might not be loaded, causing kernel panic or bootloop; I only recommend loading them into int main() if you're porting and writing   new drivers, so they start up with the device).

## Can it be ported to other AVR MCUs in the Arduino family?
Absolutely! Initially, this project was written for the ATmega32A, and since it shares the AVR architecture, porting it to an Arduino Uno or Arduino Nano would be easy due to the shared registers (except for the UART registers) and the amount of SRAM needed to be defined in the Context Switch. However, if you want to port it to larger ICs (like the ATmega2560 of the Arduino Mega) to have enough space for long-term operation, I would suggest you modify the Context Switch to make the Scheduler work – it would be quite interesting. It's interesting because this project is always open source, and I want it to be able to run on various devices.

## How to Build and Run ?
1. Requirements
   - IDE: You should view and edit the Visual Code using Platform IO (you should choose the Arduino Framework because the AVR-GCC compiler is still compatible with C and ASM).
   - Hardware: An Arduino Uno R3 board uses an ATmega328, or if you don't have an Arduino, you can use the ATmega328 itself plugged into a breadboard with a 16MHz crystal and an ISP cable, or you can        use the Arduino ISP cable.
   - Connect: A USB cable (for Arduino Uno) is needed to upload the code and view the logs on Serial Monitor or Putty using the /dev/ttyACM0 port (Linux). For Arduino boards that don't use the               ATmega16U2 programmer but use the CH340, select /dev/ttyUSB0 (remember that your regular Linux account, not sudo or root, is logged into the Dialout group). If you only have the bare chip and no        board,use ISP with avrdude.
2. Project setup
   - Clone the repository:
     git clone https://github.com/nam-lab111/Vostok_micro-Kernel.git
     cd Vostok_micro-Kernel
   - Open in VS Code: Launch VS Code, go to File > Open Folder, and select the project directory. PlatformIO will automatically detect the platformio.ini file and install the necessary dependencies.
3. Build & Upload
   - Build: Click the Checkmark icon in the PlatformIO bottom bar to compile the kernel.
   - Upload: Connect your Arduino board and click the Arrow (Upload) icon.
NOTE: Because this is a custom kernel, the build process relies on specific memory mapping to handle the AVR's 2KB SRAM limitations. The platformio.ini file is configured to optimize size. If you modify the kernel configuration in avrkernel.h (e.g., change MAX_PROCS or PROC_STACK_SZ), ensure a clean build by clicking the Clean icon before uploading to avoid memory alignment issues.

## Contribute
This is a personal project by Le Khanh Nam. I welcome and encourage everyone to contribute source code, provide feedback on the project, report bugs, and suggest optimizations (as the source code still has many shortcomings regarding I/O and optimization).

## License 
This project is licensed under the MIT License - see the `LICENSE` file for details.
