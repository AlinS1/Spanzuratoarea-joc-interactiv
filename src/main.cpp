#include <../lib/LCDWIKI_GUI/LCDWIKI_GUI.h>
#include <../lib/LCDWIKI_SPI/LCDWIKI_SPI.h>
#include <Arduino.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>

#define F_CPU 16000000UL

/*
 * == Pin definitions ==
 * Buttons: A0 (PC0), A1 (PC1), A2 (PC2)
 * LEDs: A3 (PC3) - GREEN, A4 (PC4) - RED
 * Buzzer: D3 (PD3)
 * LCD: ST7735S via SPI
 */
#define BUTTON_LEFT PC0
#define BUTTON_RIGHT PC1
#define BUTTON_SELECT PC2
#define LED_G PC3
#define LED_R PC4
#define BUZZER PD3

// LCD definitions
#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define WHITE 0xFFFF
#define MODEL ST7735S

LCDWIKI_SPI my_lcd(MODEL, -1, 9, -1, 11, 8, 13, -1);
// (CS, DC, MISO, MOSI, RST, SCK, LED)

// Global variables
const char *word_list[] = {"ARDUINO", "DISPLAY", "ELECTRON", "MICROBIT",
                           "CODARE"};
char hidden_word[10];
char guessed_word[10];
char letters[11];
int word_length = 0;
int selected_index = 0;
int max_wrong_guesses = 4;
int wrong_guesses = 0;
volatile bool flag_left = false;
volatile bool flag_right = false;
volatile bool flag_select = false;

// =====================================================================
// ========================= DRAWING FUNCTIONS =========================
// =====================================================================

void drawHangman() {
	int base_x = 70;
	int base_y = 150;

	my_lcd.Set_Draw_color(WHITE);

	// Fixed - base
	my_lcd.Draw_Line(base_x, base_y, base_x + 20, base_y);
	// Fixed - pole
	my_lcd.Draw_Line(base_x + 10, base_y, base_x + 10, base_y - 60);
	// Fixed - top beam
	my_lcd.Draw_Line(base_x + 10, base_y - 60, base_x - 20, base_y - 60);
	// Fixed - rope
	my_lcd.Draw_Line(base_x - 20, base_y - 60, base_x - 20, base_y - 50);

	// Drawing based on number of wrong guesses
	if (wrong_guesses > 0) {
		// Head
		my_lcd.Draw_Circle(base_x - 20, base_y - 40, 5);
	}
	if (wrong_guesses > 1) {
		// Torso
		my_lcd.Draw_Line(base_x - 20, base_y - 35, base_x - 20, base_y - 15);
	}
	if (wrong_guesses > 2) {
		// Left arm
		my_lcd.Draw_Line(base_x - 20, base_y - 30, base_x - 30, base_y - 25);
		// Right arm
		my_lcd.Draw_Line(base_x - 20, base_y - 30, base_x - 10, base_y - 25);
	}
	if (wrong_guesses > 3) {
		// Left leg
		my_lcd.Draw_Line(base_x - 20, base_y - 15, base_x - 30, base_y - 5);
		// Right leg
		my_lcd.Draw_Line(base_x - 20, base_y - 15, base_x - 10, base_y - 5);
	}
}

void drawGuessedWord() {
	my_lcd.Fill_Rect(0, 0, 128, 40, BLACK);
	my_lcd.Set_Text_Size(2);
	my_lcd.Set_Text_colour(WHITE);
	my_lcd.Set_Text_Back_colour(BLACK);
	my_lcd.Print_String("Cuvant:", 0, 0);
	my_lcd.Print_String(guessed_word, 0, 20);
}

void drawLetterRow() {
	my_lcd.Fill_Rect(0, 40, 128, 20, BLACK);
	my_lcd.Set_Text_Size(1);
	my_lcd.Set_Text_colour(WHITE);
	my_lcd.Print_String("Litere:", 0, 40);
	for (int i = 0; i < 10; i++) {
		char ch[2] = {letters[i], '\0'};
		my_lcd.Set_Text_colour(i == selected_index ? GREEN : WHITE);
		my_lcd.Print_String(ch, 5 + i * 12, 50);
	}
}

void drawMistakeCount() {
	my_lcd.Fill_Rect(0, 70, 128, 20, BLACK);
	my_lcd.Set_Text_colour(WHITE);
	my_lcd.Set_Text_Size(1);
	my_lcd.Print_String("Greseli:", 0, 70);
	my_lcd.Print_Number_Int(wrong_guesses, 70, 70, 0, ' ', 10);
}

// =====================================================================
// =========================== ADC FUNCTIONS ===========================
// =====================================================================

void init_adc() {
	// Aref = AVcc, canal ADC6 (MUX bits 00110)
	ADMUX = (1 << REFS0) | (1 << MUX2) | (1 << MUX1);       // canal ADC6
	ADCSRA = (1 << ADEN)                                    // Enable ADC
	         | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  // prescaler 128
}

uint16_t read_adc() {
	ADCSRA |= (1 << ADSC);  // start conversie
	while (ADCSRA & (1 << ADSC))
		;  // așteaptă finalizare
	return ADC;
}

// =====================================================================
// =========================== HELPER FUNCTIONS ========================
// =====================================================================

void gpio_init() {
	// Inputs
	DDRC &= ~((1 << BUTTON_LEFT) | (1 << BUTTON_RIGHT) | (1 << BUTTON_SELECT));

	// Outputs
	DDRC |= (1 << LED_G) | (1 << LED_R);
	DDRD |= (1 << BUZZER);

	// Set initial state
	PORTC &= ~((1 << LED_G) | (1 << LED_R));
	PORTD &= ~(1 << BUZZER);
}

void feedback_on(uint8_t pin) {
	PORTC |= (1 << pin);
}

void feedback_off(uint8_t pin) {
	PORTC &= ~(1 << pin);
}

uint8_t button_pressed(uint8_t pin) {
	return (PINC & (1 << pin));
}

void displayMessage(const char *msg, int text_colour, int x) {
	my_lcd.Fill_Screen(BLACK);
	my_lcd.Set_Text_colour(text_colour);
	my_lcd.Set_Text_Back_colour(BLACK);
	my_lcd.Set_Text_Size(2);
	my_lcd.Print_String(msg, x, 60);
}

void select_random_word() {
	uint16_t seed = read_adc();
	srand(seed);  // ADC noise on pin A6
	int index = rand() % 5;
	strcpy(hidden_word, word_list[index]);
	word_length = strlen(hidden_word);

	int revealed = rand() % word_length;
	for (int i = 0; i < word_length; i++) {
		guessed_word[i] = (i == revealed) ? hidden_word[i] : '_';
	}
	guessed_word[word_length] = '\0';

	bool used[26] = {false};
	int count = 0;
	for (int i = 0; i < word_length && count < 10; i++) {
		int idx = hidden_word[i] - 'A';
		if (!used[idx]) {
			used[idx] = true;
			letters[count++] = hidden_word[i];
		}
	}
	while (count < 10) {
		char r = 'A' + rand() % 26;
		int idx = r - 'A';
		if (!used[idx]) {
			used[idx] = true;
			letters[count++] = r;
		}
	}
	// Sort letters alphabetically
	for (int i = 0; i < 9; i++) {
		for (int j = i + 1; j < 10; j++) {
			if (letters[i] > letters[j]) {
				char t = letters[i];
				letters[i] = letters[j];
				letters[j] = t;
			}
		}
	}
}

void reset_flags() {
	flag_left = flag_right = flag_select = false;
}

// =====================================================================
// ======================== TIMER FUNCTIONS ============================
// =====================================================================
#define DEBOUNCE_TIME_MS 1000
#define RESET_TIME_S 8

volatile bool debounce_active = false;

void init_timer0_debounce() {
	TCCR0A = (1 << WGM01);                               // CTC
	TCCR0B = (1 << CS01) | (1 << CS00);                  // prescaler 64
	OCR0A = (F_CPU / 64 / 1000) * DEBOUNCE_TIME_MS - 1;  // 40ms debounce
	TIMSK0 |= (1 << OCIE0A);                             // enable în ISR
	TIMSK0 &= ~(1 << OCIE0A);                            // dezactivat inițial
}

void init_timer1() {
	TCCR1A = 0;                                         // CTC mode
	TCCR1B = (1 << WGM12) | (1 << CS12) | (1 << CS10);  // CTC + prescaler 1024
	TIMSK1 = 0;  // Dezactivăm inițial întreruperea
	OCR1A = (F_CPU / 1024) * RESET_TIME_S - 1;  // 2s
}

void start_game_reset_timer() {
	TCNT1 = 0;                // Reset counter
	TIMSK1 |= (1 << OCIE1A);  // Activăm întreruperea
}

// =====================================================================
// ========================= BUTTON HANDLERS ===========================
// =====================================================================
void init_pcint() {
	// Enable PCINT for port C (PCINT[14:8])
	PCICR |= (1 << PCIE1);
	// Enable PCINT8–10 (PC0–PC2)
	PCMSK1 |= (1 << PCINT8) | (1 << PCINT9) | (1 << PCINT10);
	// Enable global interrupts
	sei();
}

void button_left() {
	selected_index = (selected_index - 1 + 10) % 10;
	drawLetterRow();
}

void button_right() {
	selected_index = (selected_index + 1) % 10;
	drawLetterRow();
}

void button_select() {
	char selected_letter = letters[selected_index];
	bool correct = false;
	for (int i = 0; i < word_length; i++) {
		if (hidden_word[i] == selected_letter) {
			guessed_word[i] = selected_letter;
			correct = true;
		}
	}
	if (correct) {
		feedback_on(LED_G);
		tone(BUZZER, 1000, 150);
		drawGuessedWord();
		feedback_off(LED_G);
	} else {
		feedback_on(LED_R);
		tone(BUZZER, 300, 300);
		wrong_guesses++;
		drawMistakeCount();
		drawHangman();
		feedback_off(LED_R);
	}

	if (strcmp(guessed_word, hidden_word) == 0) {
		displayMessage("YOU WIN!", GREEN, 16);
		start_game_reset_timer();
	}
	if (wrong_guesses >= max_wrong_guesses) {
		displayMessage("GAME OVER!", RED, 8);
		start_game_reset_timer();
	}
}

volatile uint8_t pending_button = 0;
// 0 = nimic, 1 = left, 2 = right, 3 = select

ISR(PCINT1_vect) {
	if (debounce_active)
		return;

	// Detectăm care buton a fost apăsat și setăm doar unul
	if (PINC & (1 << PC0)) {
		pending_button = 1;  // left
	} else if (PINC & (1 << PC1)) {
		pending_button = 2;  // right
	} else if (PINC & (1 << PC2)) {
		pending_button = 3;  // select
	} else {
		return;  // niciun buton relevant apăsat
	}

	debounce_active = true;
	PCICR &= ~(1 << PCIE1);   // dezactivează întreruperile
	TIMSK0 |= (1 << OCIE0A);  // pornește timerul de debounce
}

ISR(TIMER0_COMPA_vect) {
	switch (pending_button) {
		case 1:
			if (PINC & (1 << PC0))
				flag_left = true;
			break;
		case 2:
			if (PINC & (1 << PC1))
				flag_right = true;
			break;
		case 3:
			if (PINC & (1 << PC2))
				flag_select = true;
			break;
	}

	pending_button = 0;
	debounce_active = false;
	TIMSK0 &= ~(1 << OCIE0A);  // dezactivează temporar Timer0
	PCICR |= (1 << PCIE1);     // reactivează întreruperile
}

ISR(TIMER1_COMPA_vect) {
	TIMSK1 &= ~(1 << OCIE1A);  // Oprim întreruperea

	// Resetează jocul
	wrong_guesses = 0;
	selected_index = 0;
	flag_left = false;
	flag_right = false;
	flag_select = false;

	select_random_word();
	my_lcd.Fill_Screen(BLACK);
	drawGuessedWord();
	drawLetterRow();
	drawMistakeCount();
	drawHangman();
}

// =====================================================================
// ============================= MAIN ==================================
// =====================================================================

void setup() {
	gpio_init();
	init_adc();
	init_pcint();
	init_timer0_debounce();
	init_timer1();

	my_lcd.Init_LCD();
	my_lcd.Fill_Screen(BLACK);

	select_random_word();
	drawGuessedWord();
	drawLetterRow();
	drawMistakeCount();
	drawHangman();
}

void loop() {
	if (flag_left) {
		button_left();
		reset_flags();
	} else if (flag_right) {
		button_right();
		reset_flags();
	} else if (flag_select) {
		button_select();
		reset_flags();
	}
}
