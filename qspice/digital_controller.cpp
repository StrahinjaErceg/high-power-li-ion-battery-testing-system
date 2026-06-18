#include <malloc.h>
#include <math.h>

union uData {
   bool b; char c; unsigned char uc; short s; unsigned short us;
   int i; unsigned int ui; float f; double d;
   long long int i64; unsigned long long int ui64;
   char *str; unsigned char *bytes;
};

int __stdcall DllMain(void *module, unsigned int reason, void *reserved) { return 1; }

struct sDIGITAL_CONTROLLER {
   int state;
   double target_v_ramp;
   double v_int;
   double i_int1, i_int2, i_int3;
   double wait_timer;
   double prev_time;
   double d1, d2, d3;
   double relay_out;
};

extern "C" __declspec(dllexport) void digital_controller(struct sDIGITAL_CONTROLLER **opaque, double t, union uData *data) {
   // ====================================================================
   // 1. READ RAW ADC VOLTAGES (0 to 3.3V from the physical sensors)
   // ====================================================================
   double Vbuck_adc = data[ 0].d;
   double iBuck_adc = data[ 1].d;
   double Vbus_adc  = data[ 2].d;
   double Vbat_adc  = data[ 3].d;
   double i1_adc    = data[ 4].d;
   double i2_adc    = data[ 5].d;
   double i3_adc    = data[ 6].d;

   // Outputs
   double &Switch = data[ 7].d;
   double &PWM1_H = data[ 8].d;
   double &PWM1_L = data[ 9].d;
   double &PWM2_H = data[10].d;
   double &PWM2_L = data[11].d;
   double &PWM3_H = data[12].d;
   double &PWM3_L = data[13].d;

   // ====================================================================
   // 2. SENSOR DECODING (Converting 3.3V signals back to real physics)
   // ====================================================================

   // Voltage Dividers (100k Top, 4.7k Bottom -> Scaling factor is 0.04489)
   double Vbuck = Vbuck_adc / 0.04489;
   double Vbus  = Vbus_adc  / 0.04489;
   double Vbat  = Vbat_adc  / 0.04489;

   // Master Current Sensor (INA240A1 Gain 20 * 0.5mOhm Shunt -> 0.01 V/A)
   double iBuck = iBuck_adc / 0.025;

   // Phase Current Sensors (INA240A4 Gain 200 * 0.5mOhm Shunt -> 0.1 V/A)
   double i1 = i1_adc / 0.025;
   double i2 = i2_adc / 0.025;
   double i3 = i3_adc / 0.025;

   // ====================================================================
   // 3. PI CONTROL & STATE MACHINE
   // ====================================================================

   if(!*opaque) {
      *opaque = (struct sDIGITAL_CONTROLLER *) malloc(sizeof(struct sDIGITAL_CONTROLLER));
      (*opaque)->state = 1;
      (*opaque)->target_v_ramp = 0.0;
      (*opaque)->v_int = 0.0;
      (*opaque)->i_int1 = (*opaque)->i_int2 = (*opaque)->i_int3 = 0.0;
      (*opaque)->wait_timer = 0.0;
      (*opaque)->prev_time = t;
      (*opaque)->d1 = (*opaque)->d2 = (*opaque)->d3 = 0.0;
      (*opaque)->relay_out = 0.0;
   }
   struct sDIGITAL_CONTROLLER *inst = *opaque;

   double dt = t - inst->prev_time;
   bool advance_state = (dt > 0.0);
   if (dt <= 0.0) dt = 1e-9;

   double safe_vbus = (Vbus < 5.0) ? 5.0 : Vbus;

   // --- MAIN BATTERY TESTING PARAMETERS ---
   const double TARGET_BAT_V = 58.8; // Updated for 14S pack
   const double TARGET_BAT_I = 50.0; // The actual charge current into the battery

   // --- PRE-CHARGE SAFETY LIMITS (Decoupled from battery charging) ---
   const double PRECHARGE_I_LIMIT = 10.0; // Safe limit just for the empty buck capacitors
   const double TOTAL_BUCK_CAP = 0.009;   // 9000uF
   // Calculate a guaranteed safe ramp rate based only on the pre-charge limit
   const double RAMP_RATE = (PRECHARGE_I_LIMIT / TOTAL_BUCK_CAP) * 0.8;

   switch (inst->state) {
      case 1: { // PRE-CHARGE RAMP
         inst->relay_out = 0.0;
         if (advance_state) {
            if (inst->target_v_ramp < Vbat) inst->target_v_ramp += RAMP_RATE * dt;
            else inst->target_v_ramp = Vbat;
         }

         double err_v_pre = inst->target_v_ramp - Vbuck;
         if (advance_state) inst->v_int += (50.0 * err_v_pre * dt);

         double i_cmd_total = (10.0 * err_v_pre) + inst->v_int;
         // Clamp the pre-charge current to the safety limit, NOT the battery target
         if (i_cmd_total > PRECHARGE_I_LIMIT) i_cmd_total = PRECHARGE_I_LIMIT;
         if (i_cmd_total < 0.0) i_cmd_total = 0.0;
         double i_cmd_ph = i_cmd_total / 3.0;

         if (advance_state) {
            inst->i_int1 += 50.0 * (i_cmd_ph - i1) * dt;
            inst->i_int2 += 50.0 * (i_cmd_ph - i2) * dt;
            inst->i_int3 += 50.0 * (i_cmd_ph - i3) * dt;
         }

         inst->d1 = (inst->target_v_ramp / safe_vbus) + (0.01 * (i_cmd_ph - i1)) + inst->i_int1;
         inst->d2 = (inst->target_v_ramp / safe_vbus) + (0.01 * (i_cmd_ph - i2)) + inst->i_int2;
         inst->d3 = (inst->target_v_ramp / safe_vbus) + (0.01 * (i_cmd_ph - i3)) + inst->i_int3;

         if (inst->target_v_ramp >= Vbat && Vbuck >= (Vbat - 1.0)) {
            if (advance_state) inst->wait_timer += dt;
            if (inst->wait_timer > 0.005) {
                inst->state = 2;
                inst->wait_timer = 0;
                inst->i_int1 = inst->i_int2 = inst->i_int3 = 0.0;
                inst->v_int = 0.0;
            }
         }
         break;
      }
      case 2: { // CLOSE RELAY
         inst->relay_out = 10.0;
         inst->d1 = inst->d2 = inst->d3 = (Vbat / safe_vbus);
         if (advance_state) inst->wait_timer += dt;
         if (inst->wait_timer > 0.002) {
             inst->state = 3;
             inst->wait_timer = 0;
             inst->i_int1 = inst->i_int2 = inst->i_int3 = 0.0;
         }
         break;
      }
      case 3: { // CONSTANT CURRENT (Battery Charging)
         inst->relay_out = 10.0;
         // Now using your TARGET_BAT_I freely
         double i_target_ph = TARGET_BAT_I / 3.0;
         if (advance_state) {
            inst->i_int1 += 50.0 * (i_target_ph - i1) * dt;
            inst->i_int2 += 50.0 * (i_target_ph - i2) * dt;
            inst->i_int3 += 50.0 * (i_target_ph - i3) * dt;
         }

         inst->d1 = (Vbat/safe_vbus) + (0.01 * (i_target_ph - i1)) + inst->i_int1;
         inst->d2 = (Vbat/safe_vbus) + (0.01 * (i_target_ph - i2)) + inst->i_int2;
         inst->d3 = (Vbat/safe_vbus) + (0.01 * (i_target_ph - i3)) + inst->i_int3;

         if (Vbat >= TARGET_BAT_V) {
             inst->state = 4;
             inst->v_int = TARGET_BAT_I;
         }
         break;
      }
      case 4: { // CONSTANT VOLTAGE
         inst->relay_out = 10.0;
         double err_v_cv = TARGET_BAT_V - Vbat;
         if (advance_state) inst->v_int += (20.0 * err_v_cv * dt);

         double i_cmd_cv = (5.0 * err_v_cv) + inst->v_int;
         if (i_cmd_cv > TARGET_BAT_I) i_cmd_cv = TARGET_BAT_I;
         if (i_cmd_cv < 0.0) i_cmd_cv = 0.0;
         double i_cmd_cv_ph = i_cmd_cv / 3.0;

         if (advance_state) {
            inst->i_int1 += 50.0 * (i_cmd_cv_ph - i1) * dt;
            inst->i_int2 += 50.0 * (i_cmd_cv_ph - i2) * dt;
            inst->i_int3 += 50.0 * (i_cmd_cv_ph - i3) * dt;
         }

         inst->d1 = (TARGET_BAT_V/safe_vbus) + (0.01 * (i_cmd_cv_ph - i1)) + inst->i_int1;
         inst->d2 = (TARGET_BAT_V/safe_vbus) + (0.01 * (i_cmd_cv_ph - i2)) + inst->i_int2;
         inst->d3 = (TARGET_BAT_V/safe_vbus) + (0.01 * (i_cmd_cv_ph - i3)) + inst->i_int3;
         break;
      }
   }

   // 95% Duty Cycle limit ensures GaN Bootstrap capacitors recharge
   if (!(inst->d1 >= 0.0)) inst->d1 = 0.0; if (inst->d1 > 0.95) inst->d1 = 0.95;
   if (!(inst->d2 >= 0.0)) inst->d2 = 0.0; if (inst->d2 > 0.95) inst->d2 = 0.95;
   if (!(inst->d3 >= 0.0)) inst->d3 = 0.0; if (inst->d3 > 0.95) inst->d3 = 0.95;

   if (advance_state) {
       inst->prev_time = t;
   }

   // --- 400 kHz Switching Period (1 / 400,000 = 2.5e-6) ---
   const double TSW = 2.5e-6;
   double ramp1 = fmod(t, TSW) / TSW;
   double ramp2 = fmod(t + TSW/3.0, TSW) / TSW;
   double ramp3 = fmod(t + 2.0*TSW/3.0, TSW) / TSW;

   double VG_HIGH = Vbus + 10.0;
   double VG_LOW  = 10.0;

   PWM1_H = (ramp1 < inst->d1) ? VG_HIGH : 0; PWM1_L = (ramp1 < inst->d1) ? 0 : VG_LOW;
   PWM2_H = (ramp2 < inst->d2) ? VG_HIGH : 0; PWM2_L = (ramp2 < inst->d2) ? 0 : VG_LOW;
   PWM3_H = (ramp3 < inst->d3) ? VG_HIGH : 0; PWM3_L = (ramp3 < inst->d3) ? 0 : VG_LOW;
   Switch = inst->relay_out;
}

// Updated MaxExtStepSize for 400 kHz resolution
extern "C" __declspec(dllexport) double MaxExtStepSize(struct sDIGITAL_CONTROLLER *inst, double t) { return 2.5e-8; }
extern "C" __declspec(dllexport) void Trunc(struct sDIGITAL_CONTROLLER *inst, double t, union uData *data, double *timestep) {}
extern "C" __declspec(dllexport) void Destroy(struct sDIGITAL_CONTROLLER *inst) { free(inst); }
