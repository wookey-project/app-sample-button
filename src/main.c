#include "api/syscall.h"
#include "api/types.h"
#include "api/print.h"


typedef enum {OFF, ON} led_state_t;
led_state_t display_leds = ON;

volatile bool   button_pressed = false;

device_t    button;
int         desc_button;
uint64_t    last_isr;   /* Last interrupt in milliseconds */

/*
 * User defined ISR to execute when the blue button (gpio PA0) on the STM32
 * discovery board is pressed.
 * Note : ISRs can use only a restricted set of syscalls. More info on kernel
 *        sources (Ada/ewok-syscalls-handler.adb or syscalls-handler.c)
 */

void exti_button_handler ()
{
    uint64_t        clock;
    e_syscall_ret   ret;

    /* Syscall to get the elapsed cpu time since the board booted */
    ret = sys_get_systick(&clock, PREC_MILLI);

    if (ret == SYS_E_DONE) {
	    /* Debounce time (in ms) */
	    if (clock - last_isr < 20) {
	        last_isr = clock;
	        return;
	    }
    }

    last_isr = clock;
    button_pressed = true;
}


int _main(uint32_t my_id)
{
    e_syscall_ret   ret;
    uint8_t         id_leds;

    printf("Hello, I'm BUTTON task. My id is %x\n", my_id);

    ret = sys_init(INIT_GETTASKID, "leds", &id_leds);
    if (ret != SYS_E_DONE) {
        printf("Task LEDS not present. Exiting.\n");
        return 1;
    }

    memset(&button, 0, sizeof(button));

    strncpy(button.name, "BUTTON", sizeof(button.name));

    button.gpio_num = 1;
    button.gpios[0].kref.port   = GPIO_PA;
    button.gpios[0].kref.pin    = 0;
    button.gpios[0].mask        = GPIO_MASK_SET_MODE | GPIO_MASK_SET_PUPD |
                                  GPIO_MASK_SET_TYPE | GPIO_MASK_SET_SPEED |
                                  GPIO_MASK_SET_EXTI;
    button.gpios[0].mode        = GPIO_PIN_INPUT_MODE;
    button.gpios[0].pupd        = GPIO_PULLDOWN;
    button.gpios[0].type        = GPIO_PIN_OTYPER_PP;
    button.gpios[0].speed       = GPIO_PIN_LOW_SPEED;
    button.gpios[0].exti_trigger = GPIO_EXTI_TRIGGER_RISE;
    button.gpios[0].exti_handler = (user_handler_t) exti_button_handler;


    ret = sys_init(INIT_DEVACCESS, &button, &desc_button);

    if (ret) {
        printf("error: sys_init() %s\n", strerror(ret));
    } else {
        printf("sys_init() - sucess\n");
    }

    /*
     * Devices and ressources registration is finished
     */

    ret = sys_init(INIT_DONE);
    if (ret) {
        printf("error INIT_DONE: %s\n", strerror(ret));
        return 1;
    }

    printf("init done.\n");

    /*
     * Main task
     */

    while (1) {
        if (button_pressed == true) {
            printf("button has been pressed\n");
            display_leds = (display_leds == ON) ? OFF : ON;

            ret = sys_ipc(IPC_SEND_SYNC, id_leds, sizeof(display_leds), (const char*) &display_leds);
            if (ret != SYS_E_DONE) {
                printf("sys_ipc(): error. Exiting.\n");
                return 1;
            }

            button_pressed = false;
        }

        sys_sleep(500, SLEEP_MODE_INTERRUPTIBLE);
    }

    return 0;
}

