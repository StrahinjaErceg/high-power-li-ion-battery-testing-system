# MATLAB/Simulink Architecture & Control Validation

This directory contains the initial system-level models and control loop validation for the bidirectional battery tester. Simulink was utilized in the early stages of development to prove the fundamental physics of the buck converter and to validate the nested Proportional-Integral (PI) control loops for Constant-Current (CC) and Constant-Voltage (CV) modes.

## Directory Contents
* **`BiBuckCharge.slx`**: The core single-phase bidirectional buck converter simulation file.
* **`Single_Phase_Simulink_Schematic.png`**: The baseline architecture used for rapid control loop iteration.
* **`Three_Phase_Simulink_Schematic.png`**: The expanded interleaved architecture layout. 
* **`Voltage_Output.png`**, **`Current_Output.png`**, **`Battery_Charge_Output.png`**: Transient scope results from the baseline single-phase simulation.

---

## The Progression: From Single-Phase to 3-Phase
Simulating a full 3-phase interleaved converter operating at a 400kHz switching frequency using Simulink's idealized switch blocks introduces immense computational overhead, often requiring days to complete a single transient run. 

To maintain agile development, the continuous-time control logic (CC/CV handoffs, voltage tracking) was validated using the **Single-Phase Model (`BiBuckCharge.slx`)**. Once the logic was proven stable here, the system architecture was scaled to the 3-phase interleaved design for the final physical hardware and QSPICE component-level modeling. 

---

## Transient Waveform Analysis (Single-Phase Baseline)
The provided scope outputs demonstrate the system's behavior prior to implementing the 3-phase interleaved upgrade. Analyzing these baseline results highlights exactly why the multiphase topology was necessary for the final 50A hardware:

* **`Voltage_Output.png` (Buck Converter & Battery Voltage):** The system successfully ramps the voltage smoothly from 0V to the 37.5V battery resting threshold. The nested PI loops maintain a clean charging profile with minimal overshoot, proving the control mathematics are stable.

* **`Battery_Charge_Output.png` (State of Charge):**
  Confirms that the simulated battery model successfully accepts the charge, with the internal state rising as expected under the applied control loops.

* **`Current_Output.png` (Single-Phase Current Ripple - The Justification for 3-Phase):**
  This scope captures the critical limitation of a single-phase approach at high power. While the system successfully drives the current, the peak-to-peak ripple is massive. Fluctuations of this magnitude generate excessive thermal stress on the inductors and can damage lithium-ion cells during charging. 
  
  **Conclusion:** This single-phase simulation data directly drove the hardware design decision to upgrade to a 3-phase interleaved architecture (120° offset), which divides the 50A load into ~16.6A per phase, allowing the overlapping waveforms to cancel out this ripple and deliver clean DC power.
