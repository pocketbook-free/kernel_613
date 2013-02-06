/* linux/drivers/input/touchscreen/s3c-ts.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>

#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#define CNT_RESOLUTION_X  2048
#define CNT_RESOLUTION_Y  2048

#define SUPPORT_VIRTUAL_KEY


#define touch_state_no_touch   0
#define touch_state_one_finger 1
#define touch_state_two_finger 2

#define touch_state_VK 3

#ifdef SUPPORT_VIRTUAL_KEY			
int touch_key_code[] = {KEY_BACK, KEY_MENU, KEY_HOME, KEY_SEARCH};
#endif

struct cnt_data {
	struct input_dev	*input;
	struct delayed_work work;
	struct i2c_client	*client;	
	int intr_gpio;
	int irq;
#ifdef SUPPORT_VIRTUAL_KEY			  
  	unsigned char virtual_key;
#endif  
};

static struct cnt_data *tsdata;
static struct workqueue_struct *cnt_wq;

/*
 * The functions for inserting/removing us as a module.
 */
static int cnt_touch_recv_data(struct i2c_client *client, uint8_t *Rdbuf)
{
	int ret;
	ret = i2c_master_recv(client, Rdbuf, sizeof(Rdbuf));
	if(ret != 8)
	{
		printk("receive data ret != 8\n");
		return -1;
	}
	printk("data buf[0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n", Rdbuf[0], Rdbuf[1], Rdbuf[2], Rdbuf[3],
			Rdbuf[4], Rdbuf[5], Rdbuf[6], Rdbuf[7]);
	return 0;
}

static int cnt_touch_report_data(struct i2c_client *client, uint8_t *Rdbuf)
{
	int x0,y0,x1,y1;
	int touch_state = 0;

	if(Rdbuf[0]==0x00) //Check the data if valid
	{ 
		printk("data unvalid\n");
	}
	else if((Rdbuf[0]>>6)==0x01)//Scan finger number on panel
	{
		if((Rdbuf[0]&0x03)==0x01)
		{
			touch_state=touch_state_one_finger;
		}
		else if((Rdbuf[0]&0x03)==0x02)
		{
			touch_state=touch_state_two_finger;	
		}
		else if((Rdbuf[0]&0x03)==0x0)
		{
			touch_state=touch_state_no_touch;
		}

		x0=((Rdbuf[1]&0xF0)<<4)|Rdbuf[2];
		y0=((Rdbuf[1]&0x0F)<<8)|Rdbuf[3];
		x1=((Rdbuf[4]&0xF0)<<4)|Rdbuf[5];
		y1=((Rdbuf[4]&0x0F)<<8)|Rdbuf[6];

		/*convert*/
		x0 = (x0*600/2048);
		y0 = (y0*800/2048);
		x1 = (x1*600/2048);
		y1 = (y1*800/2048);
	//	printk("touch_state:%d\n", touch_state);
		
	}

	if(!touch_state)
	{
		input_report_key(tsdata->input, BTN_TOUCH, 0);	
		input_report_key(tsdata->input, BTN_2, 0);
		input_report_abs(tsdata->input, ABS_MT_POSITION_X, 0);
		input_report_abs(tsdata->input, ABS_MT_POSITION_Y, 0);
	}
	else if(touch_state)
	{
		input_report_key(tsdata->input, BTN_TOUCH, 1);	
		input_report_abs(tsdata->input, ABS_MT_POSITION_X, y0);
		input_report_abs(tsdata->input, ABS_MT_POSITION_Y, 600 - x0);
		input_mt_sync(tsdata->input);

		if(touch_state == 2)
		{
			input_report_abs(tsdata->input, ABS_MT_POSITION_X, y1);
			input_report_abs(tsdata->input, ABS_MT_POSITION_Y, 600 - x1);
			input_mt_sync(tsdata->input);
		}
	//	printk("x0=%d, y0=%d, x1=%d, y1=%d\n", y0, 600 - x0, y1, 600 - x1);
	}
	input_sync(tsdata->input);
}
static void cnt_touch_work_func(struct work_struct *work)
{
	printk("%s\n", __func__);

	char CheckSum_value;
	int i, ret;
	struct i2c_client *client;
	client = tsdata->client;
	unsigned char Rdbuf[8] = {0};

	ret = i2c_master_recv(client, Rdbuf, sizeof(Rdbuf));
	if(ret != 8)
	{
		printk("receive data ret != 8\n");
		enable_irq(tsdata->irq);
		return -1;
	}
//	printk("data buf[0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n", Rdbuf[0], Rdbuf[1], Rdbuf[2], Rdbuf[3],
//			Rdbuf[4], Rdbuf[5], Rdbuf[6], Rdbuf[7]);

	CheckSum_value=0;
	for(i=0;i<8;i++)
	{
		CheckSum_value += Rdbuf[i];
	}

	if(CheckSum_value!=00) 
	{
		enable_irq(tsdata->irq);
		return;
	} // checksum error

	cnt_touch_report_data(client, Rdbuf);    
out:
	enable_irq(tsdata->irq);
}

static irqreturn_t cnt_touch_interrupt(int irq, void *dev_id)
{
	disable_irq_nosync(tsdata->irq);
	queue_work(cnt_wq, &tsdata->work.work);
	return IRQ_HANDLED;
}

static int s3c_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{              
	printk("%s\n", __func__);
	struct input_dev *input;
	int err;
	unsigned char buf_recv[4] = {0};
	err = i2c_master_recv(client, buf_recv, 4);
	if(err < 0)
	{
		printk("err:%d\n", err);
		goto fail2;
	
	}

	tsdata = kzalloc(sizeof(struct cnt_data ), GFP_KERNEL);
	if(!tsdata)
	{
		printk("failed to allocate driver data\n");
		return -ENOMEM;
	}


	cnt_wq = create_singlethread_workqueue("cnt_wq");
	if(!cnt_wq)
	{
	err = -ENOMEM;
	goto fail;
	}
	 
	tsdata->client = client;
	strlcpy(client->name, "cnt-touch", I2C_NAME_SIZE);
	INIT_DELAYED_WORK(&tsdata->work, cnt_touch_work_func);

	input = input_allocate_device();
	if(!input)
	{
		printk("failed to allocate input device\n");
		return -ENOMEM;
	}

	input->name = "cnt-touch";
	input->id.bustype = BUS_I2C;
	input->dev.parent = &client->dev;
	tsdata->input  = input;
	tsdata->irq = client->irq;
	tsdata->intr_gpio = 174;
	input_set_drvdata(input, tsdata);

	// Initial input device
	input->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, CNT_RESOLUTION_X, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, CNT_RESOLUTION_Y, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);

#ifdef SUPPORT_VIRTUAL_KEY				
	set_bit(KEY_MENU, input->keybit);
	set_bit(KEY_HOME, input->keybit);
	set_bit(KEY_SEARCH, input->keybit);
	set_bit(KEY_BACK, input->keybit);
#endif	
	set_bit(BTN_2, input->keybit);

	err = input_register_device(input);
	if (err < 0)
	{
	dev_err(&client->dev, "Register input device fail\n");
	goto fail;
	}

	dev_err(&client->dev, "Register input device OK\n");

	if(request_irq(tsdata->irq, cnt_touch_interrupt, IRQF_TRIGGER_RISING, client->name, tsdata))
	{
	dev_err(&client->dev, "Unable to requst touchscreen IRQ");
	input_unregister_device(input);
	input = NULL;
	}

	return 0;

fail:
	input_free_device(tsdata->input);
	if(cnt_wq)
	destroy_workqueue(cnt_wq);
fail2:
	return err;
}

static int s3c_ts_remove(struct i2c_client *client)
{
	struct cnt_data *tsdata = i2c_get_clientdata(client);

	flush_workqueue(cnt_wq);
	input_unregister_device(tsdata->input);
	input_free_device(tsdata->input);


	kfree(tsdata );
	return 0;
}

static struct i2c_device_id cnt_touch_idtable[] = {
	{ "cnt-touch", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, cnt_touch_idtable);

static struct i2c_driver cnt_touch_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name	= "cnt-touch"
	},
	.id_table	= cnt_touch_idtable,
	.probe	= s3c_ts_probe,
	.remove	= s3c_ts_remove,
};

static int __init s3c_ts_init(void)
{
	printk("%s\n", __func__);
	int ret;
	ret = i2c_add_driver(&cnt_touch_driver);
	printk("the ret : %d\n", ret);
	return ret;
}

static void __exit s3c_ts_exit(void)
{
	i2c_del_driver(&cnt_touch_driver);
}

module_init(s3c_ts_init);
module_exit(s3c_ts_exit);

MODULE_AUTHOR("Jack Hsueh");
MODULE_DESCRIPTION("X9 CNT driver");
MODULE_LICENSE("GPL");
