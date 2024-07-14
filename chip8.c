#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

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
        .pixel_outlines = true //default pixel outlines
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
void update_screen(const sdl_t sdl , const config_t config , chip8_t chip8) {
    SDL_Rect rect = {.x = 0 , .y = 0 , .w = config.scale_factor, .h = config.scale_factor} ;

    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF ;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF ;
    const uint8_t bg_b = (config.bg_color >> 8) & 0xFF ;
    const uint8_t bg_a = (config.bg_color ) & 0xFF ;

    const uint8_t fg_r = (config.fg_color >> 24) & 0xFF ;
    const uint8_t fg_g = (config.fg_color >> 16) & 0xFF ;
    const uint8_t fg_b = (config.fg_color >> 8) & 0xFF ;
    const uint8_t fg_a = (config.fg_color ) & 0xFF ;

    for ( uint32_t i = 0 ; i < sizeof chip8.display ; i ++) {
        //translate i to X and Y
        rect.x = (i % config.window_width)* config.scale_factor ;
        rect.y = (i / config.window_width)* config.scale_factor ;

        if ( chip8.display[i]) {
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

//to detect any input every time screeen refreshes
void handle_input(chip8_t *chip8) {
    SDL_Event event ;

    while ( SDL_PollEvent(&event)) {
        switch ( event.type ) {
            case SDL_QUIT:
                //Exit window and end program
                chip8->state = QUIT ;
                return;   
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        //Escape key quits
                        chip8->state = QUIT ;
                        return;
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
                    default:
                        break;
                }
                break ;  
            case SDL_KEYUP:  
                break;
            default:
                break ;  
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
            printf( "jump to address 0x%03X\n", chip8 -> inst.NNN) ;
            break ;
        case 0x02:
            //0x2NNN: call subroutine at NNN (push instruction to stack)
            printf("Store address 0x%04X and jump to NNN (0x%03X)\n", *chip8 ->stack_top, chip8 -> inst.NNN) ;
            break  ;
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
        case 0x0A:
            // 0xANNN: Set index register to NNN
            printf( "Set I to NNN (0x%03X)\n" , chip8 -> inst.NNN) ;
            break;
        case 0x0D:
            //0xDXYN: Draw sprite at coords VX,VY of height N
            //sprite XORs the screen where drawn
            //VF(carry flag) is set if any pixels are turned off, useful for collisions???
            uint8_t X_coord = chip8 -> V[chip8 -> inst.X] % config.window_width;
            uint8_t Y_coord = chip8 -> V[chip8 -> inst.Y] % config.window_height;
            //const uint8_t original_X = X_coord ;
            chip8 -> V[0xF] = 0 ; //initialize carry flag to 0???

            printf ( "Display sprite at V%X,V%x (%u,%u) of height N (%u).\n" ,chip8 -> inst.X , chip8 -> inst.Y, X_coord, Y_coord,chip8 -> inst.N) ;
            //loop for N rows
            // for ( uint8_t i = 0 ; i < chip8 -> inst.N ; i ++) {
            //     X_coord = original_X ; // reset X
            //     const uint8_t sprite_data = chip8 -> ram[chip8 -> I + i] ; //I is address of sprite data, i is offset(each sprite is 1 byte wide)

            //     for ( int j = 7 ; j >= 0 ; j --) {
            //         bool *pixel = &chip8 -> display[Y_coord*config.window_width + X_coord] ;
            //         const bool sprite_bit = ((sprite_data<<j)&1) ;

            //         if ( sprite_bit && *pixel) chip8 -> V[0x0F] = 1 ; // carry flag condition
            //         *pixel ^= sprite_bit ; // XOR pixel with data

            //         if ( ++X_coord >= config.window_width) break ;  // right edge case
            //     }
                
            //     if ( ++Y_coord >= config.window_height) break ;  // bottom edge case
            // }

            break;
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
        case 0x0A:
            // 0xANNN: Set index register to NNN
            chip8 -> I = chip8 -> inst.NNN ;
            break;
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

    //main emulator loop
    while (chip8.state != QUIT) {
        //handle user input
        handle_input(&chip8) ;

        if (chip8.state == PAUSED) continue ;

        //Get_time()
        //Emulate chip8 instruction
        emulate_instruction(&chip8 , config) ;
        //Get_time

        //Delay for 60 fps ~ 16.67
        SDL_Delay(17) ;

        // Update window with changes
        update_screen(sdl , config , chip8) ;
    }


    //Final cleanup
    final_cleanup(sdl) ;
    
    
    puts("Testing on Windows") ;
    exit(EXIT_SUCCESS) ;


    return 1;
}