#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <SDL2/SDL.h>



// typedef __int32 int32_t;
// typedef unsigned __int32 uint32_t;

//sdl container
typedef struct {
    SDL_Window *window ;
    SDL_Renderer *renderer ;
} sdl_t ;

//configuration 
typedef struct {
    uint32_t window_width; 
    uint32_t window_height ;
    uint32_t fg_color ; //foreground RGBA
    uint32_t bg_color ; //background RGBA
    uint32_t scale_factor; //number of windows pixels one chip8 pixel will be
    bool pixel_outlines ;
    uint32_t clock_rate ; //instructions per second
} config_t ;

//states of emulator
typedef enum {
    QUIT ,
    RUNNING,
    PAUSED,
} emulator_state_t ;

//chip8 instruction format
typedef struct {
    //instruction: format DXYN
    // 4*4=16 bits or 4 hex , that is 4 bits for each letter
    //D is the instruction type or category(directly implemented while emulating in form of switch case)
    //note all of these can directly be implemented while emulating, but cmon

    uint16_t opcode ;  //the whole instruction is called opcode
    uint16_t NNN ;     //12 bit  adress/constant (rightmost 12 bits of opcode)
    uint8_t NN ;       //8 bit constant (rightmost 8 bits of opcode)
    uint8_t N ;        //4 bit constant (rightmost 4 bits of opcode)
    uint8_t X ;        //4 bit register identifier (5th to 8th bit from right)
    uint8_t Y ;        //4 bit register identifier (9th to 12th bit from right)

} instruction_t ;

//CHIP8 machine
typedef struct {
    emulator_state_t state;
    uint8_t ram[4096] ;      //RAM
    bool display[64*32] ;     // original CHIP8 resolution
    uint16_t stack[12] ;      //stack for subroutines(instructions inside instructions, the stack probably stores the addreess of the parent instructions that we have to come back to)
    uint16_t *stack_top ;      //pointer to top of stack
    uint8_t V[16] ;           //data registers from V0 to VF(to actually store temporary data)
    uint16_t I ;              //Index register, register to store indices/locations in memory
    uint16_t PC;              //program counter, stores address of instruction excecuted 
    uint8_t delay_timer;      //records delay, executes instruction when>0
    uint8_t sound_timer;      //plays sound when >0
    bool keypad[16] ;         //hexadecimal keypad
    const char *rom_name;     //currently running ROM
    instruction_t inst;       //currently executing instruction
} chip8_t ;


//initialize SDL
bool init_sdl(sdl_t *sdl , const config_t config) {
    if ( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("Could not initialize SDL subsystems!!! \n %s\n", SDL_GetError()) ;
        return false ;
    }

    sdl->window = SDL_CreateWindow("chip8", SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    config.window_width * config.scale_factor, 
                                    config.window_height * config.scale_factor,
                                    0) ;
    
    if ( !sdl->window) {
        SDL_Log("Window could not be created!!! %s\n", SDL_GetError()) ;
        return false ;
    }

    sdl->renderer = SDL_CreateRenderer ( sdl->window , -1 , SDL_RENDERER_ACCELERATED) ;
    if ( !sdl->renderer) {
        SDL_Log("Renderer could not be created!!! %s\n", SDL_GetError()) ;
        return false ;
    }


    return true ; //SUCCESSFULLY INITIALIZED
}

//setup the configuration function
bool set_config(config_t *config, const int argc, char **argv) {

    //set deafults
    *config = (config_t) {
        .window_width = 64 , //original chip8 x
        .window_height = 32, //original chip8 y
        .fg_color = 0xFFFFFFFF, //white
        .bg_color = 0x000000FF, //black
        .scale_factor = 20, //default size becomes 1280*640
        .pixel_outlines = true, //default pixel outlines
        .clock_rate = 500, //default clock rate
    } ;

    //override defaults from arguments
    for ( int i = 1 ; i < argc ; i ++) {
        (void)argv[i] ;   // prevent compiler error from unused variables
    }

    return true ;
}

//Initialize chip8 object
bool init_chip8 ( chip8_t *chip8, const char rom_name[]) {
    const uint32_t entry_point = 0x200;  //CHIP8 ROMs are loaded to 0x200
    const uint8_t  font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    } ;

    //load font
    memcpy(&chip8->ram[0] , font , sizeof(font)) ;

    //open rom file
    FILE *rom = fopen(rom_name, "rb") ;  //ROM is the .ch8 file with code/instructions and all 
    if (!rom) {
        SDL_Log("Rom file %s can't be opened, is invalid or non-existent!!!\n", rom_name) ;
        return false;
    }
    fseek(rom , 0 , SEEK_END) ;
    const size_t rom_size = ftell(rom) ;
    const size_t max_size = sizeof(chip8->ram) - entry_point;

    if ( rom_size > max_size) {
        SDL_Log("Rom file %s is tooo bigg!!! ROM size: %u , max availible CHIP8 memory: %u\n", rom_name, (unsigned)rom_size, (unsigned)max_size) ;
        return false;
    }


    rewind(rom) ; // seek to beginning of file
    if( fread( &chip8->ram[entry_point] , rom_size , 1 , rom) != 1) { //load ROM data into RAM here
        SDL_Log("Could not read rom file %s into CHIP8 memory", rom_name) ;
        return false;
    }
    fclose(rom) ; // close file after loading

    //Set CHIP8 machine defaults
    chip8 -> state = RUNNING ; //default machine state
    chip8 -> PC = entry_point ; // Start program where ROM instructions start
    chip8 -> stack_top = chip8 -> stack ;
    chip8 -> rom_name = rom_name ;
    return true ;
}

//final cleanup
void final_cleanup(const sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer) ;
    SDL_DestroyWindow(sdl.window) ;
    SDL_Quit() ; // SHUT EVERYTHING BEFORE FINISHING PROGRAM
}

//Clear screen, set to background color
void clear_screen(const sdl_t sdl, const config_t config ) {
    const uint8_t r = (config.bg_color>>24) ;
    const uint8_t g = (config.bg_color>>16) ;
    const uint8_t b = (config.bg_color>>8) ;
    const uint8_t a = (config.bg_color) ;
    SDL_SetRenderDrawColor(sdl.renderer , r, g, b, a) ;
    SDL_RenderClear(sdl.renderer) ;
}

//update screen after instructions have been processed each cycle
void update_screen(const sdl_t sdl , const config_t config , chip8_t *chip8) {
    SDL_Rect rect = {.x = 0 , .y = 0 , .w = config.scale_factor, .h = config.scale_factor} ;

    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF ;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF ;
    const uint8_t bg_b = (config.bg_color >> 8) & 0xFF ;
    const uint8_t bg_a = (config.bg_color ) & 0xFF ;

    const uint8_t fg_r = (config.fg_color >> 24) & 0xFF ;
    const uint8_t fg_g = (config.fg_color >> 16) & 0xFF ;
    const uint8_t fg_b = (config.fg_color >> 8) & 0xFF ;
    const uint8_t fg_a = (config.fg_color ) & 0xFF ;

    for ( uint32_t i = 0 ; i < sizeof chip8 -> display ; i ++) {
        //translate i to X and Y
        rect.x = (i % config.window_width)* config.scale_factor ;
        rect.y = (i / config.window_width)* config.scale_factor ;

        if ( chip8 -> display[i]) {
            //draw fg color
            SDL_SetRenderDrawColor(sdl.renderer, fg_r,fg_g, fg_b, fg_a) ;
            SDL_RenderFillRect ( sdl.renderer , &rect) ;

            //if user requests, draw baundary of pixel with bg color
            if (config.pixel_outlines){
                SDL_SetRenderDrawColor(sdl.renderer, bg_r,bg_g, bg_b, bg_a) ;
                SDL_RenderDrawRect ( sdl.renderer , &rect) ;
            }           
        }
        else {
            //draw bg color
            SDL_SetRenderDrawColor(sdl.renderer, bg_r,bg_g, bg_b, bg_a) ;
            SDL_RenderFillRect ( sdl.renderer , &rect) ;
        }
    }
    SDL_RenderPresent(sdl.renderer) ;
}

//update timers ( delay and sound )
void update_timers ( chip8_t *chip8) {
    if ( chip8 -> delay_timer > 0) chip8 -> delay_timer -- ;

    if ( chip8 -> delay_timer > 0) {
        chip8 -> delay_timer -- ;
        // play sound
    }
    else {
        //stop playing sound
    }
}

//to detect any input every time screeen refreshes
//CHIP8 Keypad Map to QUERTY:
//123C                1234
//456D                QWER
//789E                ASDF
//A0BF                ZXCV
void handle_input(chip8_t *chip8) {
    SDL_Event event ;

    while ( SDL_PollEvent(&event)) {

        switch ( event.type ) {

            case SDL_QUIT:
                //Exit window and end program
                chip8->state = QUIT ;
                break; 

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        //Escape key quits
                        chip8->state = QUIT ;
                        break ;
                    case SDLK_SPACE:
                        //spacebar , 
                        if ( chip8 -> state == RUNNING) {
                            chip8->state = PAUSED ; //pause
                            printf("paused!!!\n") ;
                        }
                        else {
                            chip8->state = RUNNING ; //Resume
                            puts("Resumed!!!\n") ;
                        }
                        break ;
                    
                    //map of qwerty to CHIP8 keypad
                    case SDLK_1: chip8 ->keypad[0x01] = true ; break;
                    case SDLK_2: chip8 ->keypad[0x02] = true ; break;
                    case SDLK_3: chip8 ->keypad[0x03] = true ; break;
                    case SDLK_4: chip8 ->keypad[0x0C] = true ; break;

                    case SDLK_q: chip8 ->keypad[0x04] = true ; break;
                    case SDLK_w: chip8 ->keypad[0x05] = true ; break;
                    case SDLK_e: chip8 ->keypad[0x06] = true ; break;
                    case SDLK_r: chip8 ->keypad[0x0D] = true ; break;

                    case SDLK_a: chip8 ->keypad[0x07] = true ; break;
                    case SDLK_s: chip8 ->keypad[0x08] = true ; break;
                    case SDLK_d: chip8 ->keypad[0x09] = true ; break;
                    case SDLK_f: chip8 ->keypad[0x0E] = true ; break;

                    case SDLK_z: chip8 ->keypad[0x01] = true ; break;
                    case SDLK_x: chip8 ->keypad[0x0A] = true ; break;
                    case SDLK_c: chip8 ->keypad[0x0B] = true ; break;
                    case SDLK_v: chip8 ->keypad[0x0F] = true ; break;
                    
                    default: break;
                }
                break ;

            case SDL_KEYUP:  
                switch (event.key.keysym.sym) {
                    //map of qwerty to CHIP8 keypad
                    case SDLK_1: chip8 ->keypad[0x01] = false ; break;
                    case SDLK_2: chip8 ->keypad[0x02] = false ; break;
                    case SDLK_3: chip8 ->keypad[0x03] = false ; break;
                    case SDLK_4: chip8 ->keypad[0x0C] = false ; break;

                    case SDLK_q: chip8 ->keypad[0x04] = false ; break;
                    case SDLK_w: chip8 ->keypad[0x05] = false ; break;
                    case SDLK_e: chip8 ->keypad[0x06] = false ; break;
                    case SDLK_r: chip8 ->keypad[0x0D] = false ; break;

                    case SDLK_a: chip8 ->keypad[0x07] = false ; break;
                    case SDLK_s: chip8 ->keypad[0x08] = false ; break;
                    case SDLK_d: chip8 ->keypad[0x09] = false ; break;
                    case SDLK_f: chip8 ->keypad[0x0E] = false ; break;

                    case SDLK_z: chip8 ->keypad[0x01] = false ; break;
                    case SDLK_x: chip8 ->keypad[0x0A] = false ; break;
                    case SDLK_c: chip8 ->keypad[0x0B] = false ; break;
                    case SDLK_v: chip8 ->keypad[0x0F] = false ; break;

                    default: break ;
                }
                break ;

            default: break ;  
        }
    }
}

#ifdef DEBUG
void print_debug_info( chip8_t *chip8, config_t config) {
    // Emulate opcode
    printf( "Address : 0x%04X, opcode: 0x%04X , Desc: ", chip8 -> PC -2 , chip8 -> inst.opcode) ;
    switch ((chip8 ->inst.opcode >>12) & 0x0F) { //this is switch for D or the type/category of instruction that the machine has to currently execute
        
        case 0x0:
            switch (chip8 -> inst.NN) {
                case 0xE0 :
                    //0x00E0: clear screen
                    printf("Clear Screen\n") ;
                    break;
                case 0XEE :
                    //0x00EE: return from subroutine (pop instruction from stack)
                    printf("Return from subroutine to address 0x%04X\n",*(chip8 ->stack_top - 1) ) ;
                    break ;
                default:
                    printf("Unimplemented opcode!!!\n") ;
                    break;
            }
            break ;

        case 0x01:
            // 0x1NNN : jump(PC) to address NNN
            printf( "jump to address NNN (0x%03X)\n", chip8 -> inst.NNN) ;
            break ;

        case 0x02:
            //0x2NNN: call subroutine at NNN (push instruction to stack)
            printf("Store address 0x%04X and jump to NNN (0x%03X)\n", *chip8 ->stack_top, chip8 -> inst.NNN) ;
            break  ;

        case 0x03:
            //0x3XNN: skip next instruction if VX==NN
            printf ( "If V%X == NN (0x%02X == 0x%02X), skip next instruction\n",chip8 -> inst.X , chip8 -> V[chip8 -> inst.X],chip8 -> inst.NN);
            break ;

        case 0x04:
            //0x4XNN: skip next instruction if VX!=NN
            printf ( "If V%X != NN (0x%02X != 0x%02X), skip next instruction\n",chip8 -> inst.X , chip8 -> V[chip8 -> inst.X],chip8 -> inst.NN);
            break ;

        case 0x05:
            //0x5XY0: skip next instruction if VX==VY
            if (chip8 -> inst.N != 0){
                printf("Invalid opcode!!!\n"); //invalid opcode
                break;
            }
            printf ( "If V%X == V%X  (0x%02X == 0x%02X), skip next instruction\n",chip8 -> inst.X ,chip8 -> inst.Y, chip8 -> V[chip8 -> inst.X],chip8 -> V[chip8 -> inst.Y]);
            break ;

        case 0x06:
            //0x6NNN: Set register VX to NN
            //basically put NN in register V[X]
            printf( "Set V%X to NN (0x%02X)\n" , chip8 -> inst.X , chip8 -> inst.NN) ;
            break;

        case 0x07:
            //0x7NNN: Add NN to VX
            //basically V[X] += NN
            printf( "Add: V%X += NN (0x%02X)\n" , chip8 -> inst.X , chip8 -> inst.NN) ;
            break;

        case 0x08: 
            //0x8XYN:VX VY related ALU instructions
            switch ( chip8 -> inst.N) {
                case 0x0 :
                    //Set VX = VY
                    printf( "Set V%X (0x%02X) to V%X (0x%02X)\n" , chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] , chip8 -> inst.Y , chip8 -> V[chip8 -> inst.Y] ) ;
                    break;
                case 0x01 :
                    //Set VX |= VY
                    printf( "Set V%X (0x%02X) | = V%X (0x%02X)\n" , chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] , chip8 -> inst.Y , chip8 -> V[chip8 -> inst.Y] ) ;
                    break ;
                case 0x02 :
                    //Set VX &= VY
                    printf( "Set V%X (0x%02X) &= V%X (0x%02X)\n" , chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] , chip8 -> inst.Y , chip8 -> V[chip8 -> inst.Y] ) ;
                    break ;
                case 0x03 :
                    //Set VX ^= VY
                    printf( "Set V%X (0x%02X) ^= V%X (0x%02X)\n" , chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] , chip8 -> inst.Y , chip8 -> V[chip8 -> inst.Y] ) ;
                    break ;
                case 0x04 :
                    //Set VX += VY, VF is for overflow, VF = 1 if carry
                    printf( "Set V%X (0x%02X) += V%X (0x%02X)\n" , chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] , chip8 -> inst.Y , chip8 -> V[chip8 -> inst.Y] ) ;
                    break ;
                case 0x05 :
                    //Set VX -= VY, VF is for underflow, VF = 0 if borrow
                    printf( "Set V%X (0x%02X) -= V%X (0x%02X)\n" , chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] , chip8 -> inst.Y , chip8 -> V[chip8 -> inst.Y] ) ;
                    break;
                case 0x06 :
                    //Set VX >>= 1, VF is leftmost bit before shift
                    printf( "Set V%X (0x%02X) >>= 1\n" , chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] ) ;
                    break;
                case 0x07 :
                    //Set VX = VY - VX, VF is for underflow, VF = 0 if borrow
                    printf( "Set V%X (0x%02X) -= V%X (0x%02X), VX = - VX\n" , chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] , chip8 -> inst.Y , chip8 -> V[chip8 -> inst.Y] ) ;
                    break;
                case 0x0E :
                    //Set VX >>= 1, VF is leftmost bit before shift
                    printf( "Set V%X (0x%02X) <<= 1\n" , chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] ) ;
                    break;
                default :
                    printf("Invalid opcode!!!\n") ;
                    break ; //invalid
            }
            break ;

        case 0x09:
            //0x9XY0: skip next instruction if VX!=VY
            if (chip8 -> inst.N != 0){
                printf("Invalid opcode!!!\n"); //invalid opcode
                break;
            }
            printf ( "If V%X == V%X  (0x%02X != 0x%02X), skip next instruction\n",chip8 -> inst.X ,chip8 -> inst.Y, chip8 -> V[chip8 -> inst.X],chip8 -> V[chip8 -> inst.Y]);
            break ;

        case 0x0A:
            // 0xANNN: Set index register to NNN
            printf( "Set I to NNN (0x%03X)\n" , chip8 -> inst.NNN) ;
            break;

        case 0x0B:
            // 0xBNNN: jump to V0 + NNN
            printf( "jump to address V0 (0x%02X) + NNN (0x%03X)\n", chip8 -> V[0], chip8 -> inst.NNN) ;
            break;

        case 0x0C:
            // 0xCXNN: Sets VX = rand(0,255) & NN 
            printf( "Set V%X = rand() %% 256 & NN (0x%02X)\n" , chip8 -> inst.X , chip8 -> inst.NN ) ;
            break ;

        case 0x0D:
            //0xDXYN: Draw sprite at coords VX,VY of height N
            //sprite XORs the screen where drawn
            //VF(carry flag) is set if any pixels are turned off, useful for collisions???
            uint8_t X_coord = chip8 -> V[chip8 -> inst.X] % config.window_width;
            uint8_t Y_coord = chip8 -> V[chip8 -> inst.Y] % config.window_height;
            //const uint8_t original_X = X_coord ;
            chip8 -> V[0xF] = 0 ; //initialize carry flag to 0???

            printf ( "Display sprite at V%X,V%X (%u,%u) of height N (%u).\n" ,chip8 -> inst.X , chip8 -> inst.Y, X_coord, Y_coord,chip8 -> inst.N) ;

            break;

        case 0x0E:
            //0xEXNN: key pressed if statements
            switch (chip8 -> inst.NN) {
                case 0x09E:
                    //0xEX9E: if key stored in VX is pressed, skip instruction
                    printf ( "If key stored in V%X (0x%02X) is pressed, skip next instruction\n", chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] ) ;
                    break ;
                case 0x0A1:
                    //0xEXA1: if key stored in VX is not pressed, skip instruction
                    printf ( "If key stored in V%X (0x%02X) is not pressed, skip next instruction\n", chip8 -> inst.X , chip8 -> V[chip8 -> inst.X] ) ;
                    break ;
                default:
                    printf("Invalid opcode!!!\n") ;
                    break ; //invalid
            }
            break ;

        case 0x0F:
            //0xFXNN: misc with register VX
            switch ( chip8 -> inst.NN) {
                case 0x07 :
                    //0xVX07: sets VX to delay timer
                    printf("Sets V%X = delay timer (0x%02X)\n", chip8 -> inst.X , chip8 -> delay_timer ) ;
                    break ;
                case 0x0A :
                    //0xVX07: await for a keypress, then store first keypress in VX
                    printf( "Wait till key pree, store at V%X\n", chip8 -> inst.X ) ;
                    break ;
                case 0x15 :
                    //0xFX15: Set delay timer to VX
                    printf( "Set delay timer to V%X (0x%02X)\n",chip8 -> inst.X,chip8 -> V[chip8 -> inst.X]) ;
                    break ;    
                case 0x18 :
                    //0xFX15: Set sound timer to VX
                    printf( "Set sound timer to V%X (0x%02X)\n",chip8 -> inst.X,chip8 -> V[chip8 -> inst.X]) ;
                    break ;
                case 0x1E :
                    //0xFX15: Set I += VX
                    printf("Set I (0x%04X) += V%X (0x%02X)\n",chip8 -> I,chip8 -> inst.X,chip8 -> V[chip8 -> inst.X] ) ;
                    break ;  
                case 0x29 :
                    //0xFX29: Set I to location of sprite/font of char stored in VX(0x0-0xF) from RAM
                    if ((chip8 -> V[chip8 -> inst.X]) > 0xF) {
                        printf("VX stores value > F\n") ;
                        break ; //font not availible
                    }
                    printf( "Set I to the sprite location in V%X (0x%02X)\n", chip8 -> inst.X,chip8 -> V[chip8 -> inst.X]) ;
                    break;
                case 0x33 :
                    //0xFX33: Store BCD(VX(0-255)) at location I,I+1,I+2; eg. if VX=205 and I=5 then ram[5]=2,ram[6]=0 ,ram[7]=5
                    printf("Store BCD at V%X (0x%02X) in RAM starting from location I (0x%04X)\n", chip8 -> inst.X,chip8 -> V[chip8 -> inst.X], chip8 -> I) ;
                    break;    
                case 0x55 :
                    //0xFX55: Dump V0 to VX in ram starting from indesx stored at I, basically ram[I]=V0, ram[I+1]=V1 ...ram[I+X] = V[x]
                    printf( "Dump V0 to V%X into RAM starting from location I (0x%04X)\n", chip8 -> inst.X,  chip8 -> I) ;
                    break;
                case 0x65 :
                    //0xFX65: Load registers V0 to VX with ram[I] to ram[I+X], opposite of above
                    printf( "Load V0 to V%X from RAM starting from location I (0x%04X)\n", chip8 -> inst.X,  chip8 -> I) ;
                    break;
                default :
                    break ; //invalid
            }
            break ;

        default :
            printf("Unimplemented opcode!!!\n") ;
            break; //for invalid/unimplemented instructions
    }
}


#endif

//emulate 1 CHIP8 instruction
void emulate_instruction(chip8_t *chip8 , const config_t config) {
    //get opcode/instruction from PC which points to opcode to be executed in the ram
    chip8 -> inst.opcode = chip8->ram[chip8->PC] << 8 | chip8->ram[chip8->PC + 1] ;
    chip8 -> PC += 2 ;  //increment the PC before itself for location of next opcode

    //format DXYN
    //update current instruction codes: N,NN,NN...
    chip8 ->inst.NNN = chip8 -> inst.opcode & 0x0FFF ;
    chip8 ->inst.NN = chip8 -> inst.opcode & 0x0FF ;
    chip8 ->inst.N = chip8 -> inst.opcode & 0x0F ;
    chip8 ->inst.X = (chip8 -> inst.opcode >>8) & 0x0F ;
    chip8 ->inst.Y = (chip8 -> inst.opcode >>4) & 0x0F ;

#ifdef DEBUG
print_debug_info(chip8, config) ;
#endif

    // Emulate opcode
    switch ((chip8 ->inst.opcode >>12) & 0x0F) { //this is switch for D or the type/category of instruction that the machine has to currently execute
        
        case 0x0:
            switch (chip8 -> inst.NN) {
                case 0xE0 :
                    //0x00E0: clear screen
                    memset(&chip8 -> display[0] , false, sizeof ( chip8 -> display ) ) ;
                    break ;
                case 0XEE :
                    //0x00EE: return from subroutine (pop instruction from stack)
                    chip8 -> PC = *--chip8 -> stack_top ;
                    break ;
                default:
                    break;
            }
            break ;

        case 0x01:
            // 0x1NNN : jump(PC) to address NNN
            chip8 -> PC = chip8 -> inst.NNN ;
            break ;

        case 0x02:
            //0x2NNN: call subroutine at NNN (push instruction to stack)
            *chip8 -> stack_top ++ = chip8 -> PC ; // save current address of instruction to stack (for returning back to it later)
            chip8 -> PC = chip8 -> inst.NNN ; // make PC point to address of subroutine which will be next instruction
            break ;

        case 0x03:
            //0x3XNN: skip next instruction if VX==NN
            if ( chip8 -> V[chip8 ->inst.X] == chip8 ->inst.NN) chip8 -> PC += 2 ;
            break ;

        case 0x04:
            //0x4XNN: skip next instruction if VX!=NN
            if ( chip8 -> V[chip8 ->inst.X] != chip8 ->inst.NN) chip8 -> PC += 2 ;
            break ;

        case 0x05:
            //0x5XY0: skip next instruction if VX==VY
            if (chip8 -> inst.N != 0) break; //invalid opcode
            if ( chip8 -> V[chip8 ->inst.X] == chip8 -> V[chip8 ->inst.Y]) chip8 -> PC += 2 ;
            break ;

        case 0x06:
            //0x6NNN: Set register VX to NN
            //basically put NN in register V[X]
            chip8 -> V[chip8 ->inst.X] = chip8 ->inst.NN ;
            break;

        case 0x07:
            //0x7NNN: Add NN to VX
            //basically V[X] += NN
            chip8 -> V[chip8 ->inst.X] += chip8 ->inst.NN ;
            break;

        case 0x08: 
            //0x8XYN:VX VY related ALU instructions
            switch ( chip8 -> inst.N) {
                case 0x0 :
                    //Set VX = VY
                    chip8 -> V[chip8 -> inst.X] = chip8 -> V[chip8 -> inst.Y] ;
                    break;
                case 0x01 :
                    //Set VX |= VY
                    chip8 -> V[chip8 -> inst.X] |= chip8 -> V[chip8 -> inst.Y] ;
                    break ;
                case 0x02 :
                    //Set VX &= VY
                    chip8 -> V[chip8 -> inst.X] &= chip8 -> V[chip8 -> inst.Y] ;
                    break ;
                case 0x03 :
                    //Set VX ^= VY
                    chip8 -> V[chip8 -> inst.X] ^= chip8 -> V[chip8 -> inst.Y] ;
                    break ;
                case 0x04 :
                    //Set VX += VY, VF is for overflow, VF = 1 if carry
                    if ((uint16_t)chip8 -> V[chip8 -> inst.X] + chip8 -> V[chip8 -> inst.Y] > 255 )  chip8 -> V[0x0F] = 0x01;
                    else chip8 -> V[0x0F] = 0x0;
                    chip8 -> V[chip8 -> inst.X] += chip8 -> V[chip8 -> inst.Y] ;
                    break ;
                case 0x05 :
                    //Set VX -= VY, VF is for underflow, VF = 0 if borrow
                    if (chip8 -> V[chip8 -> inst.X] < chip8 -> V[chip8 -> inst.Y]  )  chip8 -> V[0x0F] = 0x0;
                    else chip8 -> V[0x0F] = 0x01;
                    chip8 -> V[chip8 -> inst.X] -= chip8 -> V[chip8 -> inst.Y] ;
                    break;
                case 0x06 :
                    //Set VX >>= 1, VF is leftmost bit before shift
                    chip8 -> V[0x0F] = chip8 -> V[chip8 -> inst.X] & (0x01) ;
                    chip8 -> V[chip8 -> inst.X] >>= 1 ;
                    break;
                case 0x07 :
                    //Set VX = VY - VX, VF is for underflow, VF = 0 if borrow
                    if (chip8 -> V[chip8 -> inst.X] > chip8 -> V[chip8 -> inst.Y]  )  chip8 -> V[0x0F] = 0x0;
                    else chip8 -> V[0x0F] = 0x01;
                    chip8 -> V[chip8 -> inst.X] = chip8 -> V[chip8 -> inst.Y] - chip8 -> V[chip8 -> inst.X] ;
                    break;
                case 0x0E :
                    //Set VX >>= 1, VF is leftmost bit before shift
                    chip8 -> V[0x0F] = chip8 -> V[chip8 -> inst.X] >> 7 ;
                    chip8 -> V[chip8 -> inst.X] <<= 1 ;
                    break;
                default :
                    break ; //invalid
            }
            break ;

        case 0x09:
            //0x9XY0: skip next instruction if VX!=VY
            if (chip8 -> inst.N != 0) break; //invalid opcode
            if ( chip8 -> V[chip8 ->inst.X] != chip8 -> V[chip8 ->inst.Y]) chip8 -> PC += 2 ;
            break ;

        case 0x0A:
            // 0xANNN: Set index register to NNN
            chip8 -> I = chip8 -> inst.NNN ;
            break;

        case 0x0B:
            // 0xBNNN: jump to V0 + NNN
            chip8 -> PC = chip8 -> inst.NNN  + chip8 -> V[0];
            break;

        case 0x0C:
            // 0xCXNN: Sets VX = rand(0,255) & NN 
            chip8 -> V[ chip8 -> inst.X ] = (rand() % 256) & chip8-> inst.NN ;
            break ;

        case 0x0D:
            //0xDXYN: Draw sprite at coords VX,VY of height N
            //sprite XORs the screen where drawn
            //VF(carry flag) is set if any pixels are turned off, useful for collisions???
            uint8_t X_coord = chip8 -> V[chip8 -> inst.X] % config.window_width;
            uint8_t Y_coord = chip8 -> V[chip8 -> inst.Y] % config.window_height;
            const uint8_t original_X = X_coord ;
            chip8 -> V[0xF] = 0 ; //initialize carry flag to 0???

            //loop for N rows
            for ( uint8_t i = 0 ; i < chip8 -> inst.N ; i ++) {
                X_coord = original_X ; // reset X
                const uint8_t sprite_data = chip8 -> ram[chip8 -> I + i] ; //I is address of sprite data, i is offset(each sprite is 1 byte wide)

                for ( int j = 7 ; j >= 0 ; j --) {
                    bool *pixel = &chip8 -> display[Y_coord*config.window_width + X_coord] ;
                    const bool sprite_bit = ((sprite_data>>j)&1) ;

                    if ( sprite_bit && *pixel) chip8 -> V[0x0F] = 1 ; // carry flag condition
                    *pixel ^= sprite_bit ; // XOR pixel with data

                    if ( ++X_coord >= config.window_width) break ;  // right edge case
                }

                if ( ++Y_coord >= config.window_height) break ;  // bottom edge case
            }

            break;

        case 0x0E:
            //0xEXNN: key pressed if statements
            switch (chip8 -> inst.NN) {
                case 0x09E:
                    //0xEX9E: if key stored in VX is pressed, skip instruction
                    if (chip8 ->keypad[chip8 ->V[chip8 ->inst.X]]) chip8 -> PC += 2 ;
                    break ;
                case 0x0A1:
                    //0xEXA1: if key stored in VX is not pressed, skip instruction
                    if (!chip8 ->keypad[chip8 ->V[chip8 ->inst.X]]) chip8 -> PC += 2 ;
                    break ;
                default:
                    break ; //invalid
            }
            break ;

        case 0x0F:
            //0xFXNN: misc with register VX
            switch ( chip8 -> inst.NN) {
                case 0x07 :
                    //0xVX07: sets VX to delay timer
                    chip8 -> V[chip8 -> inst.X] = chip8 -> delay_timer ;
                    break ;
                case 0x0A :
                    //0xVX07: await for a keypress, then store first keypress in VX
                    bool flag = true ;
                    for ( uint8_t i = 0 ; i < sizeof chip8 ->keypad ; i ++) {
                        if (chip8 -> keypad[i]) {
                            chip8 -> V[chip8 -> inst.X] = i ;
                            flag = false;
                            break ;
                        }
                    }
                    if ( flag ) chip8 -> PC -= 2 ; //repeat instruction if no key pressed
                    break ;
                case 0x15 :
                    //0xFX15: Set delay timer to VX
                    chip8 -> delay_timer = chip8 -> V[chip8 -> inst.X] ;
                    break ;    
                case 0x18 :
                    //0xFX15: Set sound timer to VX
                    chip8 -> sound_timer = chip8 -> V[chip8 -> inst.X] ;
                    break ;
                case 0x1E :
                    //0xFX15: Set I += VX
                    chip8 -> I += chip8 -> V[chip8 -> inst.X] ;
                    break ;  
                case 0x29 :
                    //0xFX29: Set I to location of sprite/font of char stored in VX(0x0-0xF) from RAM
                    if ((chip8 -> V[chip8 -> inst.X]) > 0xF) break ; //font not availible
                    chip8 -> I = (chip8 -> V[chip8 -> inst.X]) * 5 ;
                    break;
                case 0x33 :
                    //0xFX33: Store BCD(VX(0-255)) at location I,I+1,I+2; eg. if VX=205 and I=5 then ram[5]=2,ram[6]=0 ,ram[7]=5
                    chip8 -> ram[chip8 -> I + 2] = (chip8 -> V[chip8 -> inst.X]) % 10 ;           //ones digit store in ram[I]
                    chip8 -> ram[chip8 -> I + 1] = ((chip8 -> V[chip8 -> inst.X])/10) % 10 ;  //tens digit store in ram[I+1]
                    chip8 -> ram[chip8 -> I ] = ((chip8 -> V[chip8 -> inst.X])/100) % 10 ; //hundereds digit store in ram[I+2]
                    break;    
                case 0x55 :
                    //0xFX55: Dump V0 to VX in ram starting from indesx stored at I, basically ram[I]=V0, ram[I+1]=V1 ...ram[I+X] = V[x]
                    for ( uint8_t i = 0; i <= chip8 -> inst.X ; i ++) {
                        chip8 -> ram[chip8 -> I + i] = chip8 -> V[i] ; //dump sequentially
                    }
                    break;
                case 0x65 :
                    //0xFX65: Load registers V0 to VX with ram[I] to ram[I+X], opposite of above
                    for ( uint8_t i = 0; i <= chip8 -> inst.X ; i ++) {
                        chip8 -> V[i] = chip8 -> ram[chip8 -> I + i]  ; //load sequentially
                    }
                    break;
                default :
                    break ; //invalid
            }
            break ;

        default :
            break; //for invalid/unimplemented instructions
    }
}

//mainmain 
int main( int argc, char **argv) {


    //Default message for displaying all args
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom_name>\n", argv[0]) ;
        exit( EXIT_FAILURE) ;
    }

    //Initialize emulator configuration/options
    config_t config = {0} ;
    if (!set_config(&config, argc, argv)) exit(EXIT_FAILURE) ;

    // Initialize SDL
    sdl_t sdl = {0} ;
    if (!init_sdl(&sdl, config)) exit(EXIT_FAILURE) ;

    //Initialize machine chip8 object
    chip8_t chip8 = {0} ;
    const char *rom_name = argv[1] ;
    if ( !init_chip8(&chip8 , rom_name)) exit(EXIT_FAILURE) ;

    // initial screen clear 
    clear_screen(sdl,config) ;

    //seed random
    srand(time(NULL)) ;

    //main emulator loop
    while (chip8.state != QUIT) {
        //handle user input
        handle_input(&chip8) ;

        if (chip8.state == PAUSED) continue ;

        //Get time before instructions
        const uint64_t start_time = SDL_GetPerformanceCounter() ;

        //Emulate chip8 instructions
        for (uint32_t i = 0 ; i < config.clock_rate/60 ; i ++)
            emulate_instruction(&chip8 , config) ;

        //Get time after instructions
        const uint64_t end_time = SDL_GetPerformanceCounter() ;

        //tiime elapsed in ms(from SDL docs)
        const double time_elapsed = (double)((end_time - start_time)*1000) / SDL_GetPerformanceFrequency() ;

        //Delay for 60 fps ~ 16.67
        SDL_Delay(16.67f > time_elapsed ? 16.67f - time_elapsed : 0) ;

        // Update window with changes
        update_screen(sdl , config , &chip8) ;

        //update delay and sound timers
        update_timers(&chip8) ;
    }


    //Final cleanup
    final_cleanup(sdl) ;
    
    
    puts("Program killed!!!") ;
    exit(EXIT_SUCCESS) ;
}
