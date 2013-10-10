// Sketch for testing the backpack bus prototype, scout / master side
//
// Copyright (c) 2013, Matthijs Kooijman <matthijs@stdin.nl>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define BP_BUS_PIN 2

#define RESET_DELAY 2000
#define START_DELAY 100
#define VALUE_DELAY 550
#define SAMPLE_DELAY 250
#define IDLE_DELAY 50

void bp_reset() {
    pinMode(BP_BUS_PIN, OUTPUT);
    digitalWrite(BP_BUS_PIN, LOW);
    delayMicroseconds(RESET_DELAY);
    pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(IDLE_DELAY);
}

void bp_write_bit(uint8_t bit) {
    pinMode(BP_BUS_PIN, OUTPUT);
    digitalWrite(BP_BUS_PIN, LOW);
    delayMicroseconds(START_DELAY);
    if (bit)
        pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(VALUE_DELAY);
    pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(IDLE_DELAY);
}

uint8_t bp_read_bit() {
    uint8_t value;
    pinMode(BP_BUS_PIN, OUTPUT);
    digitalWrite(BP_BUS_PIN, LOW);
    delayMicroseconds(START_DELAY);
    pinMode(BP_BUS_PIN, INPUT);
    delayMicroseconds(SAMPLE_DELAY);
    value = digitalRead(BP_BUS_PIN);
    delayMicroseconds(VALUE_DELAY - SAMPLE_DELAY);
    // If a slave pulls the line low, wait for him to finish (to
    // prevent the idle time from disappearing because of a slow
    // slave), but don't wait forever.
    uint8_t timeout = 255;
    while(digitalRead(BP_BUS_PIN) == LOW && timeout--);
    if (!timeout) Serial.println("Bus timeout!");
    delayMicroseconds(IDLE_DELAY);
    return value;
}

bool bp_read_ready() {
    int timeout = 20;
    while (timeout--) {
        if (bp_read_bit() != LOW)
            return true;
    }
    Serial.println("Stall timeout");
    return false;
}

bool bp_read_ack_nack() {
    bool ack = false, nack = false;
    // Acks are sent as 01, nacks as 10. Since the 0 is dominant during
    // a bus conflict, a reading of 00 means both an ack and a nack was
    // sent.
    if (bp_read_bit() == LOW)
        ack = true;
    if (bp_read_bit() == LOW)
        nack = true;

    if (nack && ack)
        Serial.println("Both ACK and NAK received");
    else if (nack)
        Serial.println("NAK received");
    else if (!ack)
        Serial.println("No ACK received");
    else
        return true;

    return false;
}

enum {
    DONT_WAIT_READY = 1,
    NO_PARITY = 2,
};

bool bp_read_byte(uint8_t *b, uint8_t flags = 0) {
    bool parity_val = 0;
    *b = 0;
    uint8_t next_bit = 0x80;
    while (next_bit) {
        if (bp_read_bit()) {
            *b |= next_bit;
            parity_val ^= 1;
        }
        next_bit >>= 1;
    }
    if (!(flags & NO_PARITY)) {
        if (bp_read_bit() != parity_val) {
            Serial.println("Parity error");
            return false;
        }
    }
    if (!(flags & DONT_WAIT_READY)) {
        if(!bp_read_ready())
            return false;
    }

    return bp_read_ack_nack();
}

bool bp_write_byte(uint8_t b, uint8_t flags = 0){
    bool parity_val = 0;
    uint8_t next_bit = 0x80;
    while (next_bit) {
        if (b & next_bit)
            parity_val ^= 1;
        bp_write_bit(b & next_bit);
        next_bit >>= 1;
    }
    if (!(flags & NO_PARITY)) {
        bp_write_bit(parity_val);
    }
    if (!(flags & DONT_WAIT_READY))
        if(!bp_read_ready())
            return false;

    return bp_read_ack_nack();
}

bool bp_scan() {
    bool ok = true;
    bp_reset();
    ok = ok && bp_write_byte(0xaa);
    delay(3);
    uint8_t id[4];
    uint8_t next_addr = 0;
    while (ok) {
        for (uint8_t i = 0; i < sizeof(id) && ok; ++i) {
            ok = bp_read_byte(&id[i]);
        }

        if (!ok) {
            // Parity error, or no devices left (i.e. no ACK received)
            break;
        }

        Serial.print("Device "); Serial.print(next_addr, HEX); Serial.print(" found with id: ");
        for (uint8_t i = 0; i < sizeof(id); ++i) {
            if (id[i] < 0x10) Serial.print("0");
            Serial.print(id[i], HEX);
        }
        Serial.println();

        if (next_addr++ == 4)
            break;
    }
    return ok;
}

bool bp_read_eeprom(uint8_t addr, uint8_t offset, uint8_t *buf, uint8_t len) {
    bool ok = true;
    bp_reset();
    ok = ok && bp_write_byte(addr);
    ok = ok && bp_write_byte(0x01);
    ok = ok && bp_write_byte(offset);
    while (ok && len--)
        ok = bp_read_byte(buf++);
    return ok;
}

bool bp_write_eeprom(uint8_t addr, uint8_t offset, uint8_t *buf, uint8_t len) {
    bool ok = true;
    bp_reset();
    ok = ok && bp_write_byte(addr);
    ok = ok && bp_write_byte(0x02);
    ok = ok && bp_write_byte(offset);
    while (ok && len--) {
        ok = bp_write_byte(*buf++);
    }
    return ok;
}


void setup() {
    Serial.begin(115200);
    pinMode(3, OUTPUT);
    digitalWrite(3, LOW);
}

uint8_t eeprom_written = false;


void print_eeprom(uint8_t addr, uint8_t offset, uint8_t *buf, uint8_t len) {
    Serial.print("Device "); Serial.print(addr, HEX); Serial.println(" EEPROM:");
    Serial.print("  ");
    if (!bp_read_eeprom(addr, offset, buf, len))
        return;
    while (len--) {
        if (*buf < 0x10) Serial.print("0");
        Serial.print(*buf++, HEX);
    }
    Serial.println();
}

void loop() {
    uint8_t buf[16];
    delay(1000);
    Serial.println("Scanning...");
    digitalWrite(3, HIGH);
    digitalWrite(3, LOW);
    bp_scan();
    delay(100);
    if (!eeprom_written) {
        print_eeprom(0x00, 0, buf, sizeof(buf));
        Serial.print("Incrementing all EEPROM bytes of device 0: ");
        for (size_t i = 0; i < sizeof(buf); ++i) {
            buf[i]++;
            if (buf[i] < 0x10) Serial.print("0");
            Serial.print(buf[i], HEX);
        }
        Serial.println();
        bp_write_eeprom(0x00, 0, buf, sizeof(buf));
        eeprom_written = true;
        delay(100);
    }
    Serial.println("Reading EEPROM...");
    print_eeprom(0x00, 0, buf, sizeof(buf));
    delay(100);
    print_eeprom(0x01, 0, buf, sizeof(buf));
}

/* vim: set filetype=cpp sw=4 sts=4 expandtab: */
