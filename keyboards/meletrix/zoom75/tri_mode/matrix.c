#include <stdint.h>
#include <stdbool.h>
#include "wait.h"
#include "print.h"
#include "debug.h"
#include "util.h"
#include "matrix.h"
#include "debounce.h"
#include "quantum.h"

#include QMK_KEYBOARD_H

// Previous matrix state
matrix_row_t last_matrix[MATRIX_ROWS];

// Initialize custom matrix functionality
void matrix_init_user(void) {
    debug_enable = true;
    debug_matrix = true;
    xprintf("Custom matrix scan initialized\n");

    // Initialize last_matrix to 0
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        last_matrix[row] = 0;
    }
}

// Custom matrix scanning function
bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    bool matrix_has_changed = false;

    // Compare the current matrix state to the previous state
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        if (last_matrix[row] != current_matrix[row]) {
            matrix_has_changed = true;
            last_matrix[row] = current_matrix[row];
        }
    }

    return matrix_has_changed;
}

// Debugging function to print the matrix state
void matrix_print(void) {
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        xprintf("Row %d: %04X\n", row, last_matrix[row]);
    }
}

// Override QMK's default matrix_scan if needed
// void matrix_scan_user(void) {
//     if (matrix_scan_custom(matrix_state)) {
//         xprintf("Matrix changed!\n");
//         matrix_print();
//     }
// }
