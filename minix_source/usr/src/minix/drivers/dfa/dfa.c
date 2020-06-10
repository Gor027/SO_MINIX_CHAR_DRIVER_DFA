//
// Created by gor027 on 08.06.2020.
//

#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <stdio.h>
#include <stdlib.h>
#include <minix/ds.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ioc_dfa.h>


#define MAX_STATES 256

/* Matrix from state to alphabet to keep transitions */
static char states[MAX_STATES][MAX_STATES];

static int accepting_states[MAX_STATES]; /* Boolean value to mark state acceptable */

static int current_state;

/*
 * Function prototypes for the hello driver.
 */
static ssize_t dfa_read(devminor_t UNUSED(minor), u64_t position,
                        endpoint_t endpt, cp_grant_id_t grant, size_t size, int UNUSED(flags),
                        cdev_id_t UNUSED(id));

static ssize_t dfa_write(devminor_t minor, u64_t pos, endpoint_t ep, cp_grant_id_t gid,
                         size_t size, int UNUSED(flags), cdev_id_t UNUSED(id));

static int dfa_ioctl(devminor_t minor, unsigned long request, endpoint_t ep,
                     cp_grant_id_t gid, int UNUSED(flags), endpoint_t UNUSED(user_ep),
                     cdev_id_t UNUSED(id));

/* SEF functions and variables. */
static void sef_local_startup(void);

static int sef_cb_init(int type, sef_init_info_t *info);

static int sef_cb_lu_state_save(int);

static int lu_state_restore(void);


/* Entry points to the dfa driver. */
static struct chardriver dfa_tab =
        {
                .cdr_read    = dfa_read,
                .cdr_write   = dfa_write,
                .cdr_ioctl   = dfa_ioctl,
        };

static ssize_t dfa_read(devminor_t UNUSED(minor), u64_t position,
                        endpoint_t endpt, cp_grant_id_t grant, size_t size, int UNUSED(flags),
                        cdev_id_t UNUSED(id)) {
    char buffer[size];
    char x = (accepting_states[current_state] == 1 ? 'Y' : 'N');
    for (int i = 0; i < size; i++) {
        buffer[i] = x;
    }

    int r;
    if ((r = sys_safecopyto(endpt, grant, 0, (vir_bytes) buffer, size)) != OK)
        return r;

    return size;
}

static ssize_t dfa_write(devminor_t minor, u64_t pos, endpoint_t ep, cp_grant_id_t gid,
                         size_t size, int UNUSED(flags), cdev_id_t UNUSED(id)) {
    int r;
    char buffer[size];

    if ((r = sys_safecopyfrom(ep, gid, 0, (vir_bytes) buffer, size)) != OK) {
        return r;
    }

    /* Accept or Reject */
    for (int i = 0; i < size; i++) {
        char input = buffer[i];
        int input_in_range = input < 0 ? MAX_STATES + input : input;

        current_state = states[current_state][input_in_range];
    }

    return size;
}

/* Resets the current state and size */
int do_dfa_reset() {
    current_state = 0;

    return OK;
}

/* Adds transition between two states */
int do_dfa_add(endpoint_t ep, cp_grant_id_t gid) {
    int r;
    char triple[3];

    if ((r = sys_safecopyfrom(ep, gid, 0, (vir_bytes) triple,
                              sizeof(triple))) != OK) {
        return r;
    }

    /* Get the indices of the state and alphabet */
    int state = (triple[0] < 0 ? MAX_STATES + triple[0] : triple[0]);
    int alph = (triple[1] < 0 ? MAX_STATES + triple[1] : triple[1]);

    if (state < 0 || alph < 0) {
        printf("Add transition between %d and %d\n", state, alph);
    }

    /* Put char in states matrix to mark the transition between states */
    states[state][alph] = (triple[2] < 0 ? MAX_STATES + triple[2] : triple[2]);

    /* Reset after adding transition */
    return do_dfa_reset();
}

/* Marks a certain state to acceptable */
int do_dfa_accept(endpoint_t ep, cp_grant_id_t gid) {
    int r;
    char state;

    if ((r = sys_safecopyfrom(ep, gid, 0, (vir_bytes) &state,
                              sizeof(state))) != OK) {
        return r;
    }


    /* Get the index of the state */
    int state_index = (state < 0 ? MAX_STATES + state : state);

    if (state_index < 0) {
        printf("Accepting state %d\n", state_index);
    }

    accepting_states[state_index] = 1; /* Mark state accepting */

    return OK;
}

/* Marks a certain state to non-acceptable */
int do_dfa_reject(endpoint_t ep, cp_grant_id_t gid) {
    int r;
    char state;

    if ((r = sys_safecopyfrom(ep, gid, 0, (vir_bytes) &state,
                              sizeof(state))) != OK) {
        return r;
    }

    /* Get the index of the state */
    int state_index = (state < 0 ? MAX_STATES + state : state);

    accepting_states[state_index] = 0; /* Mark state non-accepting */

    return OK;
}

static int dfa_ioctl(devminor_t minor, unsigned long request, endpoint_t ep,
                     cp_grant_id_t gid, int UNUSED(flags), endpoint_t UNUSED(user_ep),
                     cdev_id_t UNUSED(id)) {
    /* Process I/O control requests */
    int r;

    switch (request) {
        case DFAIOCRESET:
            r = do_dfa_reset();
            return r;
        case DFAIOCADD:
            r = do_dfa_add(ep, gid);
            return r;
        case DFAIOCACCEPT:
            r = do_dfa_accept(ep, gid);
            return r;
        case DFAIOCREJECT:
            r = do_dfa_reject(ep, gid);
            return r;
    }

    return ENOTTY;
}

static int sef_cb_lu_state_save(int UNUSED(state)) {
    /* Save the state. */
    ds_publish_mem("states", states, sizeof(states), DSF_OVERWRITE);
    ds_publish_mem("accepting_states", accepting_states, sizeof(accepting_states), DSF_OVERWRITE);
    ds_publish_u32("current_state", current_state, DSF_OVERWRITE);

    return OK;
}

static int lu_state_restore() {
    /* Restore the state. */
    int states_size;
    ds_retrieve_mem("states", (char *) states, &states_size);
    ds_delete_mem("states");

    int accepting_states_size;
    ds_retrieve_mem("accepting_states", (char *) accepting_states, &accepting_states_size);
    ds_delete_mem("accepting_states");

    u32_t current_state_value;
    ds_retrieve_u32("current_state", &current_state_value);
    ds_delete_u32("current_state");
    current_state = (int) current_state_value;

    return OK;
}

static int sef_cb_init(int type, sef_init_info_t *UNUSED(info)) {
    /* Initialize the hello driver. */
    int do_announce_driver = TRUE;

    memset(states,0, sizeof(char) * MAX_STATES * MAX_STATES);
    memset(accepting_states,0, sizeof(int) * MAX_STATES);
    current_state = 0;

    switch (type) {
        case SEF_INIT_FRESH:
            break;
        case SEF_INIT_LU:
            /* Restore the state. */
            lu_state_restore();
            do_announce_driver = FALSE;
            break;
        case SEF_INIT_RESTART:
            break;
    }

    /* Announce we are up when necessary. */
    if (do_announce_driver) {
        chardriver_announce();
    }

    /* Initialization completed successfully. */
    return OK;
}

static void sef_local_startup() {
    /*
     * Register init callbacks. Use the same function for all event types
     */
    sef_setcb_init_fresh(sef_cb_init);
    sef_setcb_init_lu(sef_cb_init);
    sef_setcb_init_restart(sef_cb_init);

    /*
     * Register live update callbacks.
     */
    /* - Agree to update immediately when LU is requested in a valid state. */
    sef_setcb_lu_prepare(sef_cb_lu_prepare_always_ready);
    /* - Support live update starting from any standard state. */
    sef_setcb_lu_state_isvalid(sef_cb_lu_state_isvalid_standard);
    /* - Register a custom routine to save the state. */
    sef_setcb_lu_state_save(sef_cb_lu_state_save);

    /* Let SEF perform startup. */
    sef_startup();
}

int main(void) {
    /*
     * Perform initialization.
     */
    sef_local_startup();

    /*
     * Run the main loop.
     */
    chardriver_task(&dfa_tab);
    return OK;
}