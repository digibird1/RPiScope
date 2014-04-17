/*
	Developed by Daniel Pelikan 2013,2014
	http://digibird1.wordpress.com/
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/vmalloc.h>

int init_module(void);
void cleanup_module(void);
static int device_open(struct inode *,
	struct file *);
static int device_release(struct inode *,
	struct file *);
static ssize_t device_read(struct file *,
	char *, size_t, loff_t *);
static ssize_t device_write(struct file *,
	const char *, size_t, loff_t *);

#define SUCCESS 0
#define DEVICE_NAME "chardev"// Dev name 
#define BUF_LEN 80//Max length of device message 

//---------------------------------------------------------------------------------------------------------
//Things for the GPIO Port 

#define BCM2708_PERI_BASE       0x20000000
#define GPIO_BASE               (BCM2708_PERI_BASE + 0x200000)	// GPIO controller  
#define BLOCK_SIZE 		(4*1024)

#define INP_GPIO(g)   *(gpio.addr + ((g)/10)) &= ~(7<<(((g)%10)*3)) 
#define OUT_GPIO(g)   *(gpio.addr + ((g)/10)) |=  (1<<(((g)%10)*3)) //001
//alternative function
#define SET_GPIO_ALT(g,a) *(gpio.addr + (((g)/10))) |= (((a)<=3?(a) + 4:(a)==4?3:2)<<(((g)%10)*3))
 
#define GPIO_SET  *(gpio.addr + 7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR  *(gpio.addr + 10) // clears bits which are 1 ignores bits which are 0
 
#define GPIO_READ(g)  *(gpio.addr + 13) &= (1<<(g))


//GPIO Clock
#define CLOCK_BASE              (BCM2708_PERI_BASE + 0x00101000)
#define GZ_CLK_BUSY (1 << 7)

//---------------------------------------------------------------------------------------------------------

//How many samples to capture
#define SAMPLE_SIZE 	10000

//Define GPIO Pins

//ADC 1
#define BIT0_PIN 7
#define BIT1_PIN 8
#define BIT2_PIN 9
#define BIT3_PIN 10
#define BIT4_PIN 11
#define BIT5_PIN 25
//ADC 2
#define BIT0_PIN2 17
#define BIT1_PIN2 18
#define BIT2_PIN2 22
#define BIT3_PIN2 23
#define BIT4_PIN2 24
#define BIT5_PIN2 27

//---------------------------------------------------------------------------------------------------------

// IO Acces
static struct bcm2835_peripheral {
    unsigned long addr_p;
    int mem_fd;
    void *map;
    volatile unsigned int *addr;
};
 

static int map_peripheral(struct bcm2835_peripheral *p);
static void unmap_peripheral(struct bcm2835_peripheral *p);
static void readScope();//Read a specific sample from the scope


static int Major;		/* Major number assigned to our device driver */
static int Device_Open = 0;	/* Is device open?  
				 * Used to prevent multiple access to device */
static char msg[BUF_LEN];	/* The msg the device will give when asked */
static char *msg_Ptr;


static uint32_t *ScopeBuffer_Ptr;
static unsigned char *buf_p;


static struct file_operations fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release
};

//---------------------------------------------------------------------------------------------------------

static struct bcm2835_peripheral myclock = {CLOCK_BASE};

static struct bcm2835_peripheral gpio = {GPIO_BASE};


static struct DataStruct{
	uint32_t Buffer[SAMPLE_SIZE];
	uint32_t time;
};

struct DataStruct dataStruct;

static unsigned char *ScopeBufferStart;
static unsigned char *ScopeBufferStop;

//---------------------------------------------------------------------------------------------------------

static int map_peripheral(struct bcm2835_peripheral *p)
{
	p->addr=(uint32_t *)ioremap(GPIO_BASE, 41*4); //41 GPIO register with 32 bit (4*8)
   return 0;
}
 
static void unmap_peripheral(struct bcm2835_peripheral *p) {
 	iounmap(p->addr);//unmap the address
}
//---------------------------------------------------------------------------------------------------------
static void readScope(){

	int counter=0;
	int Fail=0;

	//disable IRQ
    local_irq_disable();
    local_fiq_disable();

	struct timespec ts_start,ts_stop;
	//Start time
	getnstimeofday(&ts_start);

    //take samples
	while(counter<SAMPLE_SIZE){
		dataStruct.Buffer[counter++]= *(gpio.addr + 13); 
	}

	//Stop time
	getnstimeofday(&ts_stop);

	//enable IRQ
    local_fiq_enable();
    local_irq_enable();

	//save the time difference
	dataStruct.time=timespec_to_ns(&ts_stop)-timespec_to_ns(&ts_start);//ns resolution

	buf_p=&dataStruct;//cound maybe removed

	ScopeBufferStart=&dataStruct;

	ScopeBufferStop=ScopeBufferStart+sizeof(struct DataStruct);
}

//---------------------------------------------------------------------------------------------------------

/*
 * This function is called when the module is loaded
 */
int init_module(void)
{
    Major = register_chrdev(0, DEVICE_NAME, &fops);

	if (Major < 0) {
	  printk(KERN_ALERT "Registering char device failed with %d\n", Major);
	  return Major;
	}

	printk(KERN_INFO "I was assigned major number %d. To talk to\n", Major);
	printk(KERN_INFO "the driver, create a dev file with\n");
	printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, Major);
	printk(KERN_INFO "Try various minor numbers. Try to cat and echo to\n");
	printk(KERN_INFO "the device file.\n");
	printk(KERN_INFO "Remove the device file and module when done.\n");

	//Map GPIO

	if(map_peripheral(&gpio) == -1) 
	{
		printk(KERN_ALERT,"Failed to map the physical GPIO registers into the virtual memory space.\n");
		return -1;
	}

	//Define Scope pins
	// Define as  Input
	INP_GPIO(BIT0_PIN);
	INP_GPIO(BIT1_PIN);
	INP_GPIO(BIT2_PIN);
	INP_GPIO(BIT3_PIN);
	INP_GPIO(BIT4_PIN);
	INP_GPIO(BIT5_PIN);

	INP_GPIO(BIT0_PIN2);
	INP_GPIO(BIT1_PIN2);
	INP_GPIO(BIT2_PIN2);
	INP_GPIO(BIT3_PIN2);
	INP_GPIO(BIT4_PIN2);
	INP_GPIO(BIT5_PIN2);

	//Set a clock signal on Pin 4
	struct bcm2835_peripheral *p=&myclock;
	p->addr=(uint32_t *)ioremap(CLOCK_BASE, 41*4);

 	INP_GPIO(4);
	SET_GPIO_ALT(4,0);

	int speed_id = 6; //1 for to start with 19Mhz or 6 to start with 500 MHz
	*(myclock.addr+28)=0x5A000000 | speed_id; //Turn off the clock
	while (*(myclock.addr+28) & GZ_CLK_BUSY) {}; //Wait untill clock is no longer busy (BUSY flag)
	*(myclock.addr+29)= 0x5A000000 | (0x32 << 12) | 0;//Set divider //divide by 50
	*(myclock.addr+28)=0x5A000010 | speed_id;//Turn clock on

	return SUCCESS;
}
//---------------------------------------------------------------------------------------------------------
/*
 * This function is called when the module is unloaded
 */
void cleanup_module(void)
{
	unregister_chrdev(Major, DEVICE_NAME);
	unmap_peripheral(&gpio);
	unmap_peripheral(&myclock);
}
//---------------------------------------------------------------------------------------------------------
/* 
 * Called when a process tries to open the device file, like
 * "cat /dev/mycharfile"
 */
static int device_open(struct inode *inode, struct file *file)
{
	static int counter = 0;

	if (Device_Open)
		return -EBUSY;

	Device_Open++;
	sprintf(msg, "I already told you %d times Hello world!\n", counter++);
	msg_Ptr = msg;

	readScope();//Read n Samples into memory

	try_module_get(THIS_MODULE);

	return SUCCESS;
}
//---------------------------------------------------------------------------------------------------------
/* 
 * Called when a process closes the device file.
 */
static int device_release(struct inode *inode, struct file *file)
{
	Device_Open--;		/* We're now ready for our next caller */
	module_put(THIS_MODULE);
	return 0;
}
//---------------------------------------------------------------------------------------------------------
/* 
 * Called when a process, which already opened the dev file, attempts to
 * read from it.
 */
static ssize_t device_read(struct file *filp,	
			   char *buffer,	
			   size_t length,
			   loff_t * offset)
{
	
	// Number of bytes actually written to the buffer 
	int bytes_read = 0;

	if (*msg_Ptr == 0)
		return 0;

	//Check that we do not overfill the buffer

	while (length && buf_p<ScopeBufferStop) {

		if(0!=put_user(*(buf_p++), buffer++))
			printk(KERN_INFO "Problem with copy\n");
		length--;
		bytes_read++;
	}

	return bytes_read;
}
//---------------------------------------------------------------------------------------------------------
/*  
 * Called when a process writes to dev file: echo "hi" > /dev/hello 
 */
static ssize_t
device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
	printk(KERN_ALERT "Sorry, this operation isn't supported.\n");
	return -EINVAL;
}
