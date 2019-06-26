#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <wiringPi.h>
#include <wiringPiSPI.h>

#define CLK 12000000
#define CHNL 0
#define INT 7

#define READ_SZ 0x200
#define BLOCK_SZ 0x2000
#define SECTOR_SZ 0x800
#define TIMING_SZ 128
#define WRITE_SZ 0x80


#define READ_CMD 0x52
#define ERASE_CMD 0xf1
#define WAKE_CMD 0x87

void addr_to_bytes(int, unsigned char *);
int bytes_to_addr(unsigned char *);
void fill_arr(unsigned char *, unsigned char, int);
void cleanup(void);

int read_page(int, int);
int write_page(int, unsigned char *);
int erase_sector(int);
int get_status();
int clear_status();
int set_interrupt();
int wake_up();
int write_buffer();

unsigned char * cmd_buffer;

int main(){
    int SPI_SETUP;
    int cleared_status = 0;
    char status;
    char opening[6] = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00};
    cmd_buffer = (unsigned char *) malloc(sizeof(unsigned char)*0x600);
    memcpy(cmd_buffer, opening, 6); 

    wiringPiSetupPhys()?: goto err_setup;
    wiringPiSPISetup(CHNL, CLK)?: goto err_spi_setup;
    
    pinMode(INT, OUTPUT);
    digitalWrite(INT, 0);

    wiringPiSPIDataRW(CHNL, cmd_buffer, 6)?:goto err_open;
    while(!cleared_status){
        printf("clearing status ...\n");
        clear_status()?:goto err_clear_status;

        get_status()?:goto err_get_status;
        status = cmd_buffer[2];
        printf("getting status: %#02x\n", status);

        if (status & 1){
            cleared_status = 1;
        }
    }
    

    
    cleanup();
    return 0;

    error:
    cleanup();
    return -1;

    err_setup:
    perror("WiringPi Setup Failed");
    goto error;

    err_spi_setup:
    perror("SPI Setup Failed");
    goto error;

    err_open:
    perror("Failure to send opening sequence");
    goto error;

    err_clear_status:
    perror("Failed to clear status");
    goto error;

    err_get_status:
    perror("Failed to get status");
    goto error;

    err_read_page:
    perror("Failed to read page");
    goto error;

    err_write_page:
    perror("Failed to write page");
    goto error;

    err_erase:
    perror("Failed to write page");
    goto error;

    err_set_int:
    perror("Failed to set interrupt");
    goto error;

    err_wake:
    perror("Failed to wake up");
    goto error;

    err_write_buffer:
    perror("Failed to write buffer");
    goto error;
}

void cleanup(void){
    free(cmd_buffer);
}

void addr_to_bytes(int addr, unsigned char * packed){
    packed[0] = (addr >> 17) & 0xFF;
    packed[1] = (addr >> 9) & 0xFF;
    packed[2] = (addr >> 7) & 0x3;
    packed[3] = addr & 0x7F;
}
int bytes_to_addr( unsigned char * addr_bytes ){
    int result;
    result = addr_bytes[0] << 17;
    result += addr_bytes[1] << 9;
    result += (addr_bytes[2] & 3) << 7;
    result += addr_bytes[3] & 0x7F;
    return result;
}

void fill_arr(unsigned char * arr, unsigned char byte, int len){
    int i;

    for (i=0;i<len;i++){
        arr[i] = byte;
    }
}

int read_page(int addr, int amt){
    if (amt > READ_SZ){
        return -2;
    } 
    unsigned char bytes[4]; 
    *cmd_buffer = READ_CMD;
    addr_to_bytes(addr, bytes);
    memcpy(cmd_buffer++, bytes, 4);

    fill_arr((cmd_buffer+5), 0xff, TIMING_SZ+amt);
    return wiringPiSPIDataRW( CHNL, cmd_buffer, 5+TIMING_SZ+amt);
}

int set_interrupt(){
    int success;
    
    char cmd[4] = {0x01, 0x00, 0x00, 0x00};
    memcpy(cmd_buffer, cmd, 4);

    success = wiringPiSPIDataRW(CHNL, cmd_buffer, 4);
    usleep(3500);
    return success;
}

int get_status(){
    unsigned char cmd[3] = {0x83, 0x00, 0xFF};
    memcpy(cmd_buffer, cmd , 3);
    
    return wiringPiSPIDataRW( CHNL, cmd_buffer, 3 );


}

int clear_status(){
    cmd_buffer[0] = 0x89;

    return wiringPiSPIDataRW( CHNL, cmd_buffer, 1 );
}

int wake_up(){
    int success;
    
    *cmd_buffer = WAKE_CMD;
    success = wiringPiSPIDataRW(CHNL, cmd_buffer, 1);

    usleep(3500);
    return success;
}

int write_page(int addr, unsigned char * data, int len){
    int ready = 0;
    
    int status_success;
    int clear_success;

    int status;
    int success;

    if (len> WRITE_SZ){
        printf("Max write size is %#20x", WRITE_SZ);
        return -2;
    }
    char addr_bytes[4];
    addr_to_bytes(addr, addr_bytes);
    
    digitalWrite(INT, 0);
    
    status_success = get_status();
    if (!status_success){
        return status_success;
    }
    
    status = cmd_buffer[2];
    while (!ready){
        status = cmd_buffer[2];
        if ((status & 1) && !(status & 0x80)){
            ready = 1;
        }else{
            printf("Waiting for card ready ..");
        }
    }
    clear_success = clear_status();
    if (!clear_success){
        return clear_success;
    }

    digitalWrite(INT, 1);
    cmd_buffer[0] = 0xf2;
    memcpy(cmd_buffer+1, addr_bytes, 4);

    memcpy(cmd_buffer+5, data, len);

    success = wiringPiSPIDataRW(CHNL, cmd_buffer, len+5);
   
    usleep(3500);
    return success;

}

int write_buffer(){
    *cmd_buffer = 0x82;
    return wiringPiSPIDataRW(CHNL, cmd_buffer, 1);
}

int erase_sector(int addr){
    int success;
    
    unsigned char addr_bytes[4];
    addr_to_bytes(addr, addr_bytes);

    unsigned char cmd[3] = {ERASE_CMD, addr_bytes[0], addr_bytes[1]};

    success = wiringPiSPIDataRW( CHNL, cmd_buffer, 3 );

    usleep(1900);//sleep 1.9ms. I don't know why but was in original python version.

    return success;
    
}

