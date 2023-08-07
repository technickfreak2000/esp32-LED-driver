#ifndef __LED_STRIP_CTL_H__
#define __LED_STRIP_CTL_H__

void init_led_strip(void);

extern uint8_t* led_strip_pixels;
extern bool update_needed;

#endif