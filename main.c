#include <stdlib.h>
#include <lpc17xx.h>
#include <RTL.h>
#include <math.h>
#include <rt_Time.h>

#include "GLCD.h"

#include "Assets/GooseFlapUp30x30.c"
#include "Assets/GooseFlapNeutral30x30.c"

#include "Assets/MedalBronze20x20.c"
#include "Assets/MedalSilver20x20.c"
#include "Assets/MedalGold20x20.c"

#include "Assets/GroundTop12x10.c"
#include "Assets/GroundBottom12x30.c"

#include "Assets/WhiteSquare10x40.c"
#include "Assets/WhiteSquare40x10.c"
#include "Assets/GreenSquare1x40.c"


/*--------------- Global Static Variables -----------------*/

uint8_t display_medal = 0; //determine medal to display (0 = none, 1 = bronze, 2 = silver, 3 = gold)

uint32_t game_time = 0;
uint32_t last_jump_time = 0; //for position calculation using delta t
uint32_t score = 0;

uint8_t game_state = 0; //Welcome Screen (0), Gameplay (1), Game Over Screen (2)
uint8_t game_speed = 4; // a value of 1-4

// uint8_t draw_ground = 1;

uint16_t delay_before_obstacle = 50;
uint8_t cur_obstacle_index = 0; 

// boolean flags
uint8_t collision = 0;
uint8_t score_changed = 0;
uint8_t button_pressed = 0;
uint8_t display_bird_up = 0;	

OS_SEM sem1, sem2, sem3, sem4;
OS_MUT mut1, mut2;


/*--------------- Defines -----------------*/


//defines for bitmap names
#define GREEN_LINE_BMP green_line_bmp 
#define White_SQUARE_40X10_BMP white_square_40x10_bmp
#define White_SQUARE_10X40_BMP white_square_10x40_bmp

#define GROUND_TOP_BMP ground_top_bmp
#define GROUND_BOTTOM_BMP ground_bottom_bmp

#define GOOSE_NEUTRAL_BMP goose_neutral_bmp
#define GOOSE_UP_BMP goose_up_bmp

#define MEDAL_BRONZE_BMP medal_bronze_bmp
#define MEDAL_SILVER_BMP medal_silver_bmp
#define MEDAL_GOLD_BMP medal_gold_bmp

//define game states
#define WELCOME_SCREEN_STATE 0
#define GAME_PLAY_STATE 1
#define GAME_OVER_SCREEN_STATE 2

#define LCD_HEIGHT 320
#define LCD_WIDTH 240

#define GRAVITY 0.002 * game_speed
#define MAX_GAME_SPEED 5
#define GROUND_HEIGHT 40

#define MEDAL_SIZE 20

#define PLAYER_JUMP_HEIGHT 30
#define PLAYER_TERMINAL_POSITION 10

#define PLAYER_POS_Y_INIT 0
#define PLAYER_X_POS 40
#define PLAYER_HEIGHT 30
#define PLAYER_WIDTH 30

#define INITIAL_OBSTACLE_OFFSET LCD_WIDTH + (50 * game_speed)
#define OBSTACLE_SIZE 40
#define NUM_OBSTACLES 3
#define NUM_OBSTACLE_INDEXES (LCD_HEIGHT - GROUND_HEIGHT) / OBSTACLE_SIZE
#define GAP_HEIGHT 2
#define OBSTACLE_SPACING 120
#define OBSTACLE_HORIZONTAL_OVERLAP_PIXELS -3

#define BITMAP_BOTTOM_PIXEL_OFFSET = 1

#define DELTA_TIME (game_time - last_jump_time)
#define GENERATE_RAND_GAP_INDEX ((rand() % (NUM_OBSTACLE_INDEXES - GAP_HEIGHT - 1)) + 1) //ensures gaps between 1 to n-1 obstacle indices


/*--------------- Relevant Structs/Typedefs -----------------*/


typedef struct {
	int16_t y_pos;
} t_player;

typedef struct {
	int32_t x_pos;
	uint16_t gap_index; //# between 2 and 5 (2 is bottom and 5 is top)
	uint8_t visible;
	uint8_t deleting;
} t_obstacle;


/*--------------- Other Global Variables -----------------*/


t_player* player_prev;
t_player* player_cur;
t_obstacle* obstacle_arr[NUM_OBSTACLES];


/*--------------- IRQs -----------------*/


//replaces need for polling button state changes
void EINT3_IRQHandler(void) 
{
	button_pressed = 1;
	LPC_GPIOINT->IO2IntClr |= (1 << 10); // clear interrupt condition once it has been fired
}


/*--------------- Peripheral Initialize/Read/Write Functions -----------------*/


void init_led(void)
{
	LPC_GPIO2->FIODIR |= 0x0000007C;
	LPC_GPIO1->FIODIR |= 0xB0000000;
}

void init_push_button(void)
{
	LPC_PINCON->PINSEL4 &= ~(3 << 20); //set push button connected to p2.10 in GPIO mode
	LPC_GPIO2->FIODIR &= ~(1 << 10); //set p2.10 to be an input pin
	LPC_GPIOINT->IO2IntEnF |= (1 << 10); //p2.10 reads falling edges to generate an interrupt
	NVIC_EnableIRQ(EINT3_IRQn); //enable IRQ
}

void init_potentiometer(void)
{
	LPC_PINCON->PINSEL1 &= ~(0x03<<18);
	LPC_PINCON->PINSEL1 |= (0x01<<18);
	
	LPC_SC->PCONP |= (0x1 << 12);
	
	LPC_ADC->ADCR = (1 << 2) |  //select AD0.2
					(4 << 8) |  //ADC clock is 25MHz/5
					(1 << 21); 	 //enable ADC
}

void set_LED(uint32_t dec)
{
	uint32_t p2_led = 0x00000000;
	uint32_t p1_led = 0x00000000;

	//clear the led before updating the score
	LPC_GPIO2->FIOCLR |= ~(p2_led);
	LPC_GPIO1->FIOCLR |= ~(p1_led);
	                                                                                   
	p2_led |= (0x1 & dec) << 6;
	p2_led |= ((0x1 << 1) & dec) << 4;
	p2_led |= ((0x1 << 2) & dec) << 2;
	p2_led |= ((0x1 << 3) & dec);
	p2_led |= ((0x1 << 4) & dec) >> 2;
	
	p1_led |= ((1 << 5) & dec) << 26;
	p1_led |= ((1 << 6) & dec) << 23;
	p1_led |= ((1 << 7) & dec) << 21;
	
	//updating the new score
	LPC_GPIO2->FIOSET |= p2_led;
	LPC_GPIO1->FIOSET |= p1_led;
}

uint16_t read_potentiometer(void)
{
	LPC_ADC->ADCR |= 1 << 24;
	
	while(!(LPC_ADC->ADGDR & (1 << 31)));
	
	return (LPC_ADC->ADGDR & (0xFFF << 4)) >> 4;
}


/*--------------- Gameplay Functions -----------------*/


void draw_ground(void)
{
	uint8_t i;
	for (i = 0; i < 20; i++)
	{
		GLCD_Bitmap(i * 12, LCD_HEIGHT - 40, 12, 10, GROUND_TOP_BMP);
		GLCD_Bitmap(i * 12, LCD_HEIGHT - 30, 12, 30, GROUND_BOTTOM_BMP);
	}
}

void clear_top_screen(void)
{
	uint8_t i;

	for (i = 0; i < 11; i++)
	{
		GLCD_ClearLn(i, 1);
	}
}

void welcome_screen(void){
	GLCD_DisplayString(3, 1, 1, "FLAPPY GOOSE");	

	GLCD_Bitmap(LCD_WIDTH/2 - PLAYER_WIDTH/2, LCD_HEIGHT/2 - PLAYER_HEIGHT, PLAYER_WIDTH, PLAYER_HEIGHT, GOOSE_NEUTRAL_BMP);

	GLCD_DisplayString(23, 10, 0, "PUSH BUTTON TO START");		

	draw_ground();
}

void game_over_screen(void){
	// GLCD_SetTextColor(White);
	char score_str[40];
	sprintf(score_str, "FINAL SCORE: %d", score);

	GLCD_Clear(White);	
	GLCD_DisplayString(4, 3, 1, "GAME OVER");
	GLCD_DisplayString(16, 13, 0, score_str);
	
	//determine medal to display
	display_medal = score / 10;
	if (display_medal == 1)
	{
		GLCD_Bitmap(LCD_WIDTH/2 - MEDAL_SIZE/2, LCD_HEIGHT/2 - MEDAL_SIZE/2, MEDAL_SIZE, MEDAL_SIZE, MEDAL_BRONZE_BMP);
		GLCD_DisplayString(23, 14, 0, "BRONZE MEDAL");
	}
	else if (display_medal == 2)
	{
		GLCD_Bitmap(LCD_WIDTH/2 - MEDAL_SIZE/2, LCD_HEIGHT/2 - MEDAL_SIZE/2, MEDAL_SIZE, MEDAL_SIZE, MEDAL_SILVER_BMP);
		GLCD_DisplayString(23, 14, 0, "SILVER MEDAL");
	}
	else if (display_medal == 3)
	{
		GLCD_Bitmap(LCD_WIDTH/2 - MEDAL_SIZE/2, LCD_HEIGHT/2 - MEDAL_SIZE/2, MEDAL_SIZE, MEDAL_SIZE, MEDAL_GOLD_BMP);	
		GLCD_DisplayString(23, 15, 0, "GOLD MEDAL");
	}
	else 
	{
		GLCD_DisplayString(19, 9, 0, "BETTER LUCK NEXT TIME");
	}
}

uint8_t detect_collisions(void)
{
	//check if obstacle passed
	if (obstacle_arr[cur_obstacle_index]->x_pos + OBSTACLE_SIZE < PLAYER_X_POS){
		score++;
		score_changed = 1;
		cur_obstacle_index = (cur_obstacle_index + 1) % 3; //make it so that the cur_obstacle_index is always 0,1,2
	}

	//collision with ground
	if (player_cur->y_pos + PLAYER_HEIGHT >= LCD_HEIGHT - GROUND_HEIGHT ) 
	{
		game_state = GAME_OVER_SCREEN_STATE;	
		return 1;
	}

	// check collisions with current obstacle in y (both top and bottom)
	if(obstacle_arr[cur_obstacle_index]->x_pos < PLAYER_X_POS + PLAYER_WIDTH){
		if ((player_cur->y_pos + PLAYER_HEIGHT > (obstacle_arr[cur_obstacle_index]->gap_index + GAP_HEIGHT) * OBSTACLE_SIZE) ||
			(player_cur->y_pos < (obstacle_arr[cur_obstacle_index]->gap_index) * OBSTACLE_SIZE))
		{
			game_state = GAME_OVER_SCREEN_STATE;
			return 1;
		}
	}

	return 0;
}


void update_obstacles_pos(void)
{
	uint8_t i;

	//update obstacle pos
	for (i = 0; i < NUM_OBSTACLES; i++)
	{
		obstacle_arr[i]->x_pos -= game_speed;
		// printf("Updating obstacle by pos %d\n", game_speed);
		

		//if obstacle not visible and its one obstacle width away, change its gap index
		if ((obstacle_arr[i]->visible == 0) && (obstacle_arr[i]->x_pos - LCD_WIDTH <= OBSTACLE_SIZE))
		{
			srand(last_jump_time); //update seed
			obstacle_arr[i]->gap_index = GENERATE_RAND_GAP_INDEX;
					
		}

		//if obstacle not visible and position is less than lcd screen width, then make it visible
		if ((obstacle_arr[i]->visible == 0) && (obstacle_arr[i]->x_pos <= LCD_WIDTH))
		{
			obstacle_arr[i]->visible = 1;
		}

		//put obstacle in delete state when it starts to disappear
		if ((obstacle_arr[i]->deleting == 0) && (obstacle_arr[i]->x_pos <= 0))
		{
			obstacle_arr[i]->deleting = 1;
		}

		//make obstacle not visible when the entire obstacle goes off screen (need to consider max game speed to allow for erasing of all pixels)
		if (obstacle_arr[i]->x_pos + OBSTACLE_SIZE + MAX_GAME_SPEED <= 0)
		{
			obstacle_arr[i]->visible = 0;
			obstacle_arr[i]->deleting = 0;
			obstacle_arr[i]->x_pos = LCD_WIDTH + OBSTACLE_SPACING - OBSTACLE_SIZE;
		}
	}
}

void draw_obstacles(void)
{
	uint16_t i = 0, j = 0;
	int16_t k = 0, erase_x_pos = 0;

	//iterate through array of obstacles
	for (i = 0; i < NUM_OBSTACLES; i++)
	{
		//only draw obstacle if it is visible
		if (obstacle_arr[i]->visible) 
		{ 
			// iterate through the number of squares to draw per obstacle
			for (j = 0; j < NUM_OBSTACLE_INDEXES; j++){

				//only draw the obstacle portion without the gap
				if((j < obstacle_arr[i]->gap_index) || (j >= obstacle_arr[i]->gap_index + GAP_HEIGHT))
				{
					erase_x_pos = obstacle_arr[i]->x_pos + OBSTACLE_SIZE;

					//erase obstacle by replacing it with white lines after its width only if its location is within the LCD display
					if (erase_x_pos <= LCD_WIDTH)
					{
						//if we are deleting an obstacle and the erase position is less than 0, make it 0 to erase uncleared obstacle portion
						if(obstacle_arr[i]->deleting && erase_x_pos < 0)
						{
							erase_x_pos = 0;
						}
						GLCD_Bitmap(erase_x_pos, j * OBSTACLE_SIZE, MAX_GAME_SPEED, OBSTACLE_SIZE, White_SQUARE_10X40_BMP);
					}
					
					if (!obstacle_arr[i]->deleting){

						//overlap the horizontal obstacle lines within the obstacle to ensure no white lines appear when game speed changes
						for (k = OBSTACLE_HORIZONTAL_OVERLAP_PIXELS; k < game_speed; k++)
						{
							if ((obstacle_arr[i])->x_pos - k <= LCD_WIDTH)
							{
								GLCD_Bitmap((obstacle_arr[i])->x_pos - k, j * OBSTACLE_SIZE, 1, OBSTACLE_SIZE, GREEN_LINE_BMP);
							}
						}
					}
				}
			}
		}
	}
}

void update_player_pos(void)
{
	int32_t player_y_pos;

	*player_prev = *player_cur;
	
	if (button_pressed)
	{
		//make player jump
		player_y_pos = player_prev->y_pos - PLAYER_JUMP_HEIGHT;

		last_jump_time = game_time;

		display_bird_up = 1;
		button_pressed = 0; //consume button press
	}
	else 
	{
		player_y_pos = (int) (player_prev->y_pos + (1.0/2.0) * GRAVITY * (int) pow(DELTA_TIME, 2)); 

		if ((player_y_pos - player_prev->y_pos) > PLAYER_TERMINAL_POSITION)
		{
			player_y_pos = player_cur->y_pos + PLAYER_TERMINAL_POSITION;
		}
	}

	if (player_y_pos <= 0)
	{
		player_cur->y_pos = 0;
	}
	else
	{
		player_cur->y_pos = player_y_pos;
	}
}

void draw_player(void)
{
	uint8_t i = 0;
	//clear previous player position
	//if player has fallen since last update
	if ((player_cur->y_pos - player_prev->y_pos) > 0)
	{
		GLCD_Bitmap(PLAYER_X_POS, player_prev->y_pos, PLAYER_WIDTH, PLAYER_TERMINAL_POSITION, White_SQUARE_40X10_BMP);
	}
	//if player has jumped since last update
	else
	{
		for (i = 0; i < (PLAYER_JUMP_HEIGHT/PLAYER_TERMINAL_POSITION); i++)
		{
			GLCD_Bitmap(PLAYER_X_POS, player_cur->y_pos + PLAYER_HEIGHT + i * PLAYER_TERMINAL_POSITION, PLAYER_WIDTH, PLAYER_TERMINAL_POSITION, White_SQUARE_40X10_BMP);		
		}
	}
	
	if (display_bird_up)
	{
		GLCD_Bitmap(PLAYER_X_POS, player_cur->y_pos, PLAYER_WIDTH, PLAYER_HEIGHT, GOOSE_UP_BMP);
		display_bird_up = 0; //consume flag	
	}
	else
	{
		GLCD_Bitmap(PLAYER_X_POS, player_cur->y_pos, PLAYER_WIDTH, PLAYER_HEIGHT, GOOSE_NEUTRAL_BMP);	
	}
}


/*--------------- Tasks -----------------*/


__task void tsk_interface_peripherals(void)
{
	os_itv_set(10);
	
	while(1){
		// output current score to led
		if (score_changed)
		{	
			set_LED(score);
			score_changed = 0; //consume score changed event
		}

		// read potentiometer value and update game speed			
		game_speed = read_potentiometer() / 1000 + 1;
		
		os_itv_wait();
	}
}

__task void tsk_update_game_physics(void)
{
	os_itv_set(1);
	
	while(1)
	{
		if(game_state == GAME_PLAY_STATE)
		{
			game_time = rt_time_get(); //increment timer
			printf("rt_time %d\n", rt_time_get());
						
			update_player_pos();
			os_sem_send(&sem1); //allow next player position to be drawn

			collision = detect_collisions();
							
			if (!collision)
			{					
				update_obstacles_pos();	
			}	

			os_sem_send(&sem2); //allow next obstacle positions to be drawn								
			
		}
		os_itv_wait();		
	}
}

__task void tsk_update_lcd(void)
{
	GLCD_Init();
	GLCD_Clear(White);
	GLCD_SetTextColor(Black);
	
	os_itv_set(1);

	while(1)
	{
		if(game_state == WELCOME_SCREEN_STATE)
		{
			welcome_screen();

			while(!button_pressed); //wait for button press
			
			clear_top_screen();
			
			game_time = rt_time_get();
			last_jump_time = game_time;
			
			button_pressed = 0; //consume button press		
			game_state = GAME_PLAY_STATE;
		} 
		else if (game_state == GAME_PLAY_STATE)
		{
			os_sem_wait(&sem1, 0xffff);		

			if(!collision)
			{								
				draw_player();				
			}

			os_sem_wait(&sem2, 0xffff);

			if(!collision)
			{																			
				draw_obstacles();
			}

		}
		else 
		{
			game_over_screen();

			while(1);
		}
		
		os_itv_wait();
	}
}

__task void tsk_start_tasks(void)
{
	os_sem_init(&sem1, 0);
	os_sem_init(&sem2, 0);

	os_tsk_create(tsk_interface_peripherals, 3); //highest priority
	os_tsk_create(tsk_update_lcd, 2); 
	os_tsk_create(tsk_update_game_physics, 1); //lowest priority
	
	os_tsk_delete_self();
}

int main(void)
{
	int i;
	
	printf("Starting Flappy Goose");

	//intializing peripherals
	init_push_button();
	init_potentiometer();
	init_led();

	//initializing player structs
	player_prev =  malloc(sizeof(t_player));
	player_cur =  malloc(sizeof(t_player));
	
	player_prev->y_pos = PLAYER_POS_Y_INIT;
	player_cur->y_pos = PLAYER_POS_Y_INIT;

	//initializing obstacle structs
	for (i = 0; i < NUM_OBSTACLES; i++) 
	{
		obstacle_arr[i] = malloc(sizeof(t_obstacle));

		obstacle_arr[i]->x_pos = INITIAL_OBSTACLE_OFFSET + i * OBSTACLE_SPACING;
		obstacle_arr[i]->visible = 0;
		obstacle_arr[i]->gap_index = 0;
		obstacle_arr[i]->deleting = 0;
	}

	//start al tasks
	os_sys_init(tsk_start_tasks);
}
