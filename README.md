# The Button app use case example

This application is coupled with the ['leds' application](../leds/README.md) as they work together using IPC channels
(Inter-Process Communication).

## Purpose of the example

This is a very basic sample application on top of the EwoK microkernel for the STM32 Discovery F407.

The purpose of this sample is to show how two applications can interact with the GPIOs and also interact
together through IPC to synchronize each other.

The current application (button) handles the PD0 GPIO in input mode (with the push Button: PA0). When
a button push event is caught, an IPC is sent to the leds application notifying it of such an event.
Hence, the button application is an IPC sender and the leds application is an IPC receiver.
 
From a user level experience, here is how the two applications should act: two of the four user LEDs
of the board are blinking (green and red, or blue and orange). When the user pushes the user Button
('blue' button on the board), the other two LEDs start blinking and the former ones stop.

In addition to the device init, GPIOs and IPC related syscalls, two time related syscalls are used in this
sample (`sys_get_systick` and `sys_yield`).

## Sketch of the example

### Getting the leds application id

The main task of the button application begins to get the task id of the leds application
using the `sys_init` syscall with `INIT_GETTASKID`:

```C
    /* Get the LEDs task id to be able to communicate with it using IPCs */
    ret = sys_init(INIT_GETTASKID, "leds", &id_leds);
```

### GPIOs configuration for the Button

Then, the GPIO associated to the button is registered.

```C
    memset (&button, 0, sizeof (button));
    strncpy (button.name, "BUTTON", sizeof (button.name));
    /*
     * Configuring the Button GPIO. Note: the related clocks are automatically set
     * by the kernel.
     * We configure one GPIO here corresponding to the STM32 Discovery F407 'blue' push button (B1):
     *     - PA0 is configured in input mode
     *
     * NOTE: we need to setup an ISR handler (exti_button_handler) to asynchronously capture the button events.
     * We only focus on the button push event, we use the GPIO_EXTI_TRIGGER_RISE configuration
     * of the EXTI trigger.
     */
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
```

It is worth noticing the ISR handler registration with:

```C
button.gpios[0].exti_handler = (user_handler_t) exti_button_handler;
```
The `GPIO_EXTI_TRIGGER_RISE` configures the IRQ associated to the GPIO to be triggered
only on a rising edge (corresponding to a button push in our case).

The `sys_init` syscall is used to initialize the device:

```C
    /* Now that the button device structure is filled, use sys_init to initialize it */
    ret = sys_init(INIT_DEVACCESS, &button, &desc_button);
```

### Leaving the initialization phase

Now that all devices have been setup, it is possible to leave the initialization phase and
move forward to the nominal one using the `sys_init` syscall:


```C
    /* Devices and resources registration is finished */
    ret = sys_init(INIT_DONE);
```

Please be aware that after the `sys_init(INIT_DONE)` milestone, no further device and resource 
registration is possible.

### ISR handler

The ISR handler is quite simple, and its main task is to set the global variable `button_pushed`
to notify the main thread of the event:

```C
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
    button_pushed = true;
}

```

The only subtlety here is the *debouncing* handling inside the ISR. When a button is pushed and because of
the mechanical constraints, there is an IRQ burst before the GPIO state becomes stable (hence the 'bouncing'
qualifier). This is why we wait for 20 milliseconds before really notifying the main thread. We perform this using
an active time measurement with a millisecond precision with the `sys_get_systick` syscall. 

### Main loop in nominal phase

After the initialization phase, the main function executes a loop that waits for notifications from the ISR
through the `button_pushed`. When the Button is indeed pushed, the button application **sends an IPC to the
leds application** as a notification to switch the blinking LEDs state. The IPC is **synchronous** as
the `IPC_SEND_SYNC` flag illustrates, meaning that the syscall is blocking and schedules the leds task immediately
(this ensures a reactive wake up of the leds app and LEDs switching when pressing the button).

```C
    while (1) {
        if (button_pressed == true) {
            printf("button has been pressed\n");

            /*
             * The button has been pressed: our LEDs internal states have
             * changed. We notify the LEDs task using a synchronous IPC. The
             * datapayload we send contains the boolean value of button_pressed.
             * Note: in our use case, sending an IPC with an empty buffer to notify a
             * button push would have been possible. We use a non empty payload only to
             * show a rich IPC example.
             */

            while((ret = sys_ipc(IPC_SEND_SYNC, id_leds, sizeof(button_pressed), (const char*) &button_pressed)) != SYS_E_DONE) {
                /* The IPC syscall has returned busy, we try to send it again */
                if (ret == SYS_E_BUSY){
                    continue;
                }
                /* This a critical IPC error (SYS_E_DENIED or SYS_E_INVAL) */
                else {
                    printf("sys_ipc(): error. Exiting.\n");
                    return 1;
                }
            }
            button_pressed = false;
        }
        ...
```

After sending the IPC to the leds task, the button task **yields** using the `sys_yield` syscall.
This puts the task in a sleep state where it will be awaken when a registered IRQ is triggered
(a button push in our case). Yielding has the advantage of releasing the CPU when there is no
job to be performed by the task.

```C
        /* Yield until the kernel awakes us for a button push */
        sys_yield();
```
