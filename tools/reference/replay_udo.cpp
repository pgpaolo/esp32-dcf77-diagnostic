// Separate host-only reference executable. GPL-3.0-or-later: linked with
// Udo Klein's original library; it is never linked into the device firmware.
#include <fstream>
#include <iostream>
#include "arduino_host.h"
#include "dcf77.cpp"
namespace Internal { namespace Generic_1_kHz_Generator {
void setup(const Clock::input_provider_t) {} // host supplies exactly one tick/sample
void isr_handler() {}
}}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    std::ifstream input(argv[1]); if(!input)return 2;
    DCF77_Clock::setup();
    uint32_t samples=0,syncedTicks=0; char c;
    while(input.get(c)) {
        if(c!='0'&&c!='1')return 2;
        Internal::Generic_1_kHz_Generator::Clock_Controller::process_1_kHz_tick_data(c=='1');
        ++samples;
        if(DCF77_Clock::get_clock_state()==Clock::synced)++syncedTicks;
    }
    std::cout<<"{\"decoder\":\"udo\",\"samples\":"<<samples
        <<",\"state\":"<<static_cast<int>(DCF77_Clock::get_clock_state())
        <<",\"syncedTicks\":"<<syncedTicks<<"}\n";
}
