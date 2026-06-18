# QSPICE Simulation & Control Verification

This directory contains the component-level circuit simulations and the compiled C++ control logic used to validate the 3-phase bidirectional battery tester power stage.

## Directory Contents
* **`multiphasebuck (5).qsch`**: The core QSPICE schematic file containing the power stage, sensing components, and load models.
* **`digital_controller.cpp` & `.dll`**: The custom C++ closed-loop control algorithm (PI cascade for CC/CV modes). Compiling this into a DLL allows QSPICE to natively execute the exact firmware logic that will run on the TI C2000 microcontroller.
* **`battery_model.cpp`**: The behavioral logic for the 2RC equivalent circuit battery model to accurately simulate charge acceptance.
* **`Qspice_Sim_Screenshot.png`**: The schematic capture of the simulation setup.
* **`Output_Graphs.png`**: The transient analysis results demonstrating successful closed-loop control.

---

## Schematic Architecture Analysis
The QSPICE schematic (`Qspice_Sim_Screenshot.png`) is designed to mirror the physical PCB implementation as closely as possible:
1. **3-Phase Power Stage:** The input splits into three parallel interleaved buck phases, utilizing power MOSFETs with highly accurate physics models (e.g., explicitly defined 1.7mΩ conduction resistances) and 4.7µH inductors.
2. **Digital Controller Block (X1):** This block acts as the microcontroller. It samples the simulated `VbuckADC`, `VbatADC`, and individual phase currents (`i1`, `i2`, `i3`). It calculates the necessary error compensations and outputs the high/low PWM signals and the `Switch` command for the SSR.
3. **SSR & Battery Model:** The output of the buck converter passes through a back-to-back MOSFET Solid State Relay (SSR) before connecting to a 2RC equivalent battery model, initialized at 37.5V.

---

## Transient Waveform Analysis
The `Output_Graphs.png` scope capture verifies that the C++ PI control loops correctly manage the power stage across a full charge cycle. 

* **Pre-Charge / Startup (0.0s - ~0.05s):** The blue trace `V(Vbuck)` ramps up smoothly before the SSR engages. This matches the converter output voltage to the resting battery voltage (approx 37.5V), ensuring there is no massive inrush current when the relay closes.
  
* **Constant Current (CC) Mode (~0.05s - 0.35s):**
  Once the SSR closes, the controller aggressively ramps the current. The orange trace `I(L1)+I(L2)+I(L3)` shows the combined output of all three phases locking perfectly onto the **50A continuous current** target. During this phase, the battery voltage (green trace) climbs linearly as the pack absorbs charge.
  
* **Constant Voltage (CV) Mode (0.35s onwards):**
  As the battery voltage approaches the fully charged 60V threshold, the cascade controller seamlessly hands over priority from the current loop to the voltage loop. The green trace `V(Vbat)` and blue trace `V(Vbuck)` plateau and hold strictly at the target voltage. Simultaneously, the output current (orange trace) exponentially decays as the battery finishes topping off, preventing overvoltage and thermal damage.

The clean transitions and lack of extreme ringing or overshoot confirm that the PI tuning parameters in the `digital_controller.cpp` file are stable and ready for hardware deployment.
