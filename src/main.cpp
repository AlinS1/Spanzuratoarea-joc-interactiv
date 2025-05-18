/*
 * Hangman Game in C for ATmega328P (Arduino Nano) without Arduino Framework
 * Uses direct register access and SPI communication with ST7735 display
 * Buttons: A0 (PC0), A1 (PC1), A2 (PC2)
 * LEDs: A3 (PC3) - GREEN, A4 (PC4) - RED
 * Buzzer: A5 (PC5)
 * LCD: ST7735S via SPI
 */

#include <Arduino.h>
#include <../lib/LCDWIKI_GUI/LCDWIKI_GUI.h>
#include <../lib/LCDWIKI_SPI/LCDWIKI_SPI.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>
#include <util/delay.h>

#define F_CPU 16000000UL

#define BUTTON_LEFT PC0
#define BUTTON_RIGHT PC1
#define BUTTON_SELECT PC2
#define LED_G PC3
#define LED_R PC4
#define BUZZER PC5

#define MODEL ST7735S
// CS, DC, MISO, MOSI, RST, SCK, LED
LCDWIKI_SPI my_lcd(MODEL, -1, 9, -1, 11, 8, 13, -1);

#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define WHITE 0xFFFF

const char *word_list[] = {"ARDUINO", "DISPLAY", "ELECTRON", "MICROBIT",
                           "CODARE"};
char hidden_word[10];
char guessed_word[10];
char letters[11];
int word_length = 0;
int selected_index = 0;
int max_wrong_guesses = 4;
int wrong_guesses = 0;

void gpio_init() {
	DDRC &= ~((1 << BUTTON_LEFT) | (1 << BUTTON_RIGHT) |
	          (1 << BUTTON_SELECT));  // Inputs
	PORTC |= (1 << BUTTON_LEFT) | (1 << BUTTON_RIGHT) |
	         (1 << BUTTON_SELECT);  // Pull-ups

	DDRC |= (1 << LED_G) | (1 << LED_R) | (1 << BUZZER);  // Outputs
	PORTC &= ~((1 << LED_G) | (1 << LED_R) | (1 << BUZZER));
}

void feedback(uint8_t pin) {
	PORTC |= (1 << pin);
	_delay_ms(150);
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

void drawHangman() {
	int base_x = 70;
	int base_y = 150;

	my_lcd.Set_Draw_color(WHITE);

	// Spânzurătoare fixă
	my_lcd.Draw_Line(base_x, base_y, base_x + 20, base_y);            // baza
	my_lcd.Draw_Line(base_x + 10, base_y, base_x + 10, base_y - 60);  // stâlp
	my_lcd.Draw_Line(base_x + 10, base_y - 60, base_x - 20,
	                 base_y - 60);  // grindă sus
	my_lcd.Draw_Line(base_x - 20, base_y - 60, base_x - 20,
	                 base_y - 50);  // sfoară

	// Desenare în funcție de greșeli
	if (wrong_guesses > 0) {
		// Cap
		my_lcd.Draw_Circle(base_x - 20, base_y - 40, 5);
	}
	if (wrong_guesses > 1) {
		// Trunchi
		my_lcd.Draw_Line(base_x - 20, base_y - 35, base_x - 20, base_y - 15);
	}
	if (wrong_guesses > 2) {
		// Mâini
		my_lcd.Draw_Line(base_x - 20, base_y - 30, base_x - 30,
		                 base_y - 25);  // stânga
		my_lcd.Draw_Line(base_x - 20, base_y - 30, base_x - 10,
		                 base_y - 25);  // dreapta
	}
	if (wrong_guesses > 3) {
		// Picioare
		my_lcd.Draw_Line(base_x - 20, base_y - 15, base_x - 30,
		                 base_y - 5);  // stâng
		my_lcd.Draw_Line(base_x - 20, base_y - 15, base_x - 10,
		                 base_y - 5);  // drept
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

void select_random_word() {
	srand(1234);  // Static seed (optionally use ADC noise)
	int index = rand() % 5;
	strcpy(hidden_word, word_list[index]);
	word_length = strlen(hidden_word);

	int revealed = rand() % word_length;
	for (int i = 0; i < word_length; i++)
		guessed_word[i] = (i == revealed) ? hidden_word[i] : '_';
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
	// sort letters
	for (int i = 0; i < 9; i++)
		for (int j = i + 1; j < 10; j++)
			if (letters[i] > letters[j]) {
				char t = letters[i];
				letters[i] = letters[j];
				letters[j] = t;
			}
}

void button_left() {
	if (button_pressed(BUTTON_LEFT)) {
		selected_index = (selected_index - 1 + 10) % 10;
		drawLetterRow();
		delay(200);
	}
}

void button_right() {
	if (button_pressed(BUTTON_RIGHT)) {
		selected_index = (selected_index + 1) % 10;
		drawLetterRow();
		delay(200);
	}
}

void button_select() {
	if (button_pressed(BUTTON_SELECT)) {
		char selected_letter = letters[selected_index];
		bool correct = false;
		for (int i = 0; i < word_length; i++) {
			if (hidden_word[i] == selected_letter) {
				guessed_word[i] = selected_letter;
				correct = true;
			}
		}
		if (correct) {
			feedback(LED_G);
			tone(BUZZER, 1000, 150);
			drawGuessedWord();
		} else {
			feedback(LED_R);
			tone(BUZZER, 300, 300);
			wrong_guesses++;
			drawMistakeCount();
			drawHangman();
		}

		delay(300);

		if (strcmp(guessed_word, hidden_word) == 0) {
			displayMessage("YOU WIN!", GREEN, 16);
			while (1)
				delay(1000);
		}
		if (wrong_guesses >= max_wrong_guesses) {
			displayMessage("GAME OVER!", RED, 8);
			while (1)
				delay(1000);
		}
	}
}

void setup() {
	gpio_init();
	my_lcd.Init_LCD();
	my_lcd.Fill_Screen(BLACK);

	select_random_word();
	drawGuessedWord();
	drawLetterRow();
	drawMistakeCount();
	drawHangman();
}

void loop() {
	while (1) {
    button_left();
    button_right();
    button_select();
	}
}
