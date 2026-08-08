#include <common.h>
#include <stdbool.h>

/*
 * This board tree references a few HMD-specific hooks, but their vendor
 * implementation is not present in this BSP drop. Provide conservative
 * defaults so we can build and keep normal boot behavior predictable.
 */
int s_is6000F;

bool get_hmd_configs(void)
{
	s_is6000F = 0;
	return false;
}

bool get_diag_flag(void)
{
	return true;
}

int lcd_low_bat(void) { 
	return 0; 
}

void display_state_verified_yellow(void) {
	
}

void display_state_verified_red(void) {
	
}

void display_state_verified_orange(void) {
	
}
