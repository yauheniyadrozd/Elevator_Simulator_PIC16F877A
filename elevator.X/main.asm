    LIST P=16F877A
    #include <P16F877A.INC>

    __CONFIG _XT_OSC & _WDT_OFF & _PWRTE_ON & _BODEN_ON & _LVP_OFF & _CP_OFF

; -------------------------
; Variables
; -------------------------
    CBLOCK 0x20
        CURRENT_FLOOR
        TARGET_FLOOR
        BUTTONS
        TEMP
        D1
        D2
    ENDC

; -------------------------
; Reset vector
; -------------------------
    ORG 0x0000
    GOTO START

; -------------------------
; Program start
; -------------------------
START:
    BSF STATUS, RP0

    ; PORTA output
    MOVLW b'11000000'
    MOVWF TRISA

    ; PORTB input
    MOVLW b'11111111'
    MOVWF TRISB

    ; PORTC output
    CLRF TRISC

    ; PORTD output
    CLRF TRISD

    ; Disable ADC, all pins digital
    MOVLW b'00000110'
    MOVWF ADCON1

    ; Enable PORTB pull-ups
    BCF OPTION_REG, NOT_RBPU

    BCF STATUS, RP0

    CLRF PORTA
    CLRF PORTC
    CLRF PORTD

    MOVLW d'1'
    MOVWF CURRENT_FLOOR
    MOVWF TARGET_FLOOR

MAIN_LOOP:
    CALL READ_BUTTONS
    CALL MOVE_ELEVATOR
    CALL SHOW_FLOOR
    GOTO MAIN_LOOP

; -------------------------
; Read buttons RB0-RB4
; Active LOW
; -------------------------
READ_BUTTONS:
    MOVF PORTB, W
    MOVWF BUTTONS
    COMF BUTTONS, F

    ; RB0 -> floor 1
    BTFSC BUTTONS, 0
    CALL SET_FLOOR_1

    ; RB1 -> floor 2
    BTFSC BUTTONS, 1
    CALL SET_FLOOR_2

    ; RB2 -> floor 3
    BTFSC BUTTONS, 2
    CALL SET_FLOOR_3

    ; RB3 -> floor 4
    BTFSC BUTTONS, 3
    CALL SET_FLOOR_4

    ; RB4 -> floor 5
    BTFSC BUTTONS, 4
    CALL SET_FLOOR_5

    RETURN

SET_FLOOR_1:
    MOVLW d'1'
    MOVWF TARGET_FLOOR
    RETURN

SET_FLOOR_2:
    MOVLW d'2'
    MOVWF TARGET_FLOOR
    RETURN

SET_FLOOR_3:
    MOVLW d'3'
    MOVWF TARGET_FLOOR
    RETURN

SET_FLOOR_4:
    MOVLW d'4'
    MOVWF TARGET_FLOOR
    RETURN

SET_FLOOR_5:
    MOVLW d'5'
    MOVWF TARGET_FLOOR
    RETURN

; -------------------------
; Move elevator one floor at a time
; RC1 = going up LED
; RC2 = going down LED
; RC0 = idle LED
; -------------------------
MOVE_ELEVATOR:
    MOVF CURRENT_FLOOR, W
    SUBWF TARGET_FLOOR, W

    ; W = TARGET - CURRENT
    BTFSC STATUS, Z
    GOTO ELEVATOR_IDLE

    BTFSS STATUS, C
    GOTO GO_DOWN

GO_UP:
    MOVLW b'00000010'
    MOVWF PORTC
    CALL DELAY
    INCF CURRENT_FLOOR, F
    RETURN

GO_DOWN:
    MOVLW b'00000100'
    MOVWF PORTC
    CALL DELAY
    DECF CURRENT_FLOOR, F
    RETURN

ELEVATOR_IDLE:
    MOVLW b'00000001'
    MOVWF PORTC
    RETURN

; -------------------------
; Show current floor
; PORTA = floor LEDs
; PORTD = 7-segment digit
; -------------------------
SHOW_FLOOR:
    ; floor LEDs on PORTA
    MOVF CURRENT_FLOOR, W
    CALL FLOOR_LED_TABLE
    MOVWF PORTA

    ; 7-segment on PORTD
    MOVF CURRENT_FLOOR, W
    CALL SEG7_TABLE
    MOVWF PORTD

    RETURN

FLOOR_LED_TABLE:
    ADDWF PCL, F
    RETLW b'00000000'
    RETLW b'00000001' ; floor 1
    RETLW b'00000010' ; floor 2
    RETLW b'00000100' ; floor 3
    RETLW b'00001000' ; floor 4
    RETLW b'00010000' ; floor 5

SEG7_TABLE:
    ADDWF PCL, F
    RETLW b'00111111' ; 0
    RETLW b'00000110' ; 1
    RETLW b'01011011' ; 2
    RETLW b'01001111' ; 3
    RETLW b'01100110' ; 4
    RETLW b'01101101' ; 5

; -------------------------
; Simple delay
; -------------------------
DELAY:
    MOVLW d'255'
    MOVWF D1
D_LOOP1:
    MOVLW d'255'
    MOVWF D2
D_LOOP2:
    DECFSZ D2, F
    GOTO D_LOOP2
    DECFSZ D1, F
    GOTO D_LOOP1
    RETURN

    END