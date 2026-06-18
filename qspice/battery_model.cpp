#include <math.h>

// Persistent variables memory structure
struct battery_state {
    double soc;             // State of Charge (0.0 to 1.0)
    double capacity_Ah;     // Total Battery Capacity in Amp-hours
    double internal_R;      // Internal series resistance in Ohms
    double last_time;       // To calculate dt
};

extern "C" __declspec(dllexport) void battery_model(
    double **in, double **out, double *time, double *idelay, int *id, int mode, battery_state **opaque)
{
    // Initialize on the very first timestep
    if (mode == 0) {
        *opaque = new battery_state;
        (*opaque)->soc = 0.1;           // Start at 10% charge
        (*opaque)->capacity_Ah = 5.0;   // 5 Ah battery pack
        (*opaque)->internal_R = 0.02;   // 20 mOhm internal resistance
        (*opaque)->last_time = 0.0;
        return;
    }

    // Cleanup on simulation end
    if (mode == 2) {
        delete *opaque;
        return;
    }

    // Main Calculation Loop (mode == 1)
    battery_state *inst = *opaque;

    // 1. Read the scaled ADC signal from ViBuck and decode it back to Amps (0.01 V/A)
    double current_adc = in[0][0];
    double current = current_adc / 0.01;

    double t = *time;
    double dt = t - inst->last_time;
    if (dt <= 0.0) dt = 1e-9;
    inst->last_time = t;

    // 2. Coulomb Counting (Integration)
    // Add (Current * time) to the SoC. 3600 converts seconds to hours.
    inst->soc += (current * dt) / (inst->capacity_Ah * 3600.0);

    // Clamp SoC between 0% and 100%
    if (inst->soc > 1.0) inst->soc = 1.0;
    if (inst->soc < 0.0) inst->soc = 0.0;

    // 3. Open Circuit Voltage (OCV) Lookup
    // A simplified linear curve for a 14-cell series pack (approx 45V empty, 58.8V full)
    double ocv = 45.0 + (inst->soc * 13.8);

    // 4. Apply Ohm's Law for voltage rise/sag based on internal resistance
    double terminal_voltage = ocv + (current * inst->internal_R);

    // ====================================================================
    // OUTPUT ROUTING (Exposing variables to Qspice)
    // ====================================================================
    // Output Pin 1 (Index 0): The physical terminal voltage for the B-source
    out[0][0] = terminal_voltage;

    // Output Pin 2 (Index 1): State of Charge (0.0 to 1.0) for plotting
    out[1][0] = inst->soc;

    // Output Pin 3 (Index 2): Open Circuit Voltage for plotting
    out[2][0] = ocv;
}
