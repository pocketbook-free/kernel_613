/* 
 * drivers/input/touchscreen/ft5x0x_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver. 
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * VERSION      	DATE			AUTHOR        Note
 *    1.0		  2010-01-05			WenFS    only support mulititouch	Wenfs 2010-10-01
 *    2.0          2011-09-05                   Duxx      Add touch key, and project setting update, auto CLB command
 *     
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include "ft5x06_ts.h"
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/gpio.h>

struct ts_event {
	u16 au16_x[CFG_MAX_TOUCH_POINTS];              //x coordinate
	u16 au16_y[CFG_MAX_TOUCH_POINTS];              //y coordinate
	u8  au8_touch_event[CFG_MAX_TOUCH_POINTS];     //touch event:  0 -- down; 1-- contact; 2 -- contact
	u8  au8_finger_id[CFG_MAX_TOUCH_POINTS];       //touch ID
	u16	pressure;
	u8  touch_point;
};

struct ft5x0x_ts_data
{
	struct input_dev *input;
	struct ts_event		event;
//	struct workqueue_struct *ts_workqueue;
	struct delayed_work work;
	struct i2c_client *client;
	int irq;
};

static struct ft5x0x_ts_data *tsdata;
struct workqueue_struct *ts_workqueue;

#if CFG_SUPPORT_TOUCH_KEY
int tsp_keycodes[CFG_NUMOFKEYS] ={

        KEY_MENU,
        KEY_HOME,
        KEY_BACK,
        KEY_SEARCH
};

char *tsp_keyname[CFG_NUMOFKEYS] ={

        "Menu",
        "Home",
        "Back",
        "Search"
};

static bool tsp_keystatus[CFG_NUMOFKEYS];
#endif
/***********************************************************************************************
Name	:	ft5x0x_i2c_rxdata 

Input	:	*rxdata
                     *length

Output	:	ret

function	:	

***********************************************************************************************/

//#if 1  //upgrade related
typedef enum
{
    ERR_OK,
    ERR_MODE,
    ERR_READID,
    ERR_ERASE,
    ERR_STATUS,
    ERR_ECC,
    ERR_DL_ERASE_FAIL,
    ERR_DL_PROGRAM_FAIL,
    ERR_DL_VERIFY_FAIL
}E_UPGRADE_ERR_TYPE;

typedef unsigned char         FTS_BYTE;     //8 bit
typedef unsigned short        FTS_WORD;    //16 bit
typedef unsigned int          FTS_DWRD;    //16 bit
typedef unsigned char         FTS_BOOL;    //8 bit

typedef struct _FTS_CTP_PROJECT_SETTING_T
{
    unsigned char uc_i2C_addr;             //I2C slave address (8 bit address)
    unsigned char uc_io_voltage;           //IO Voltage 0---3.3v;	1----1.8v
    unsigned char uc_panel_factory_id;     //TP panel factory ID
}FTS_CTP_PROJECT_SETTING_T;

#define FTS_NULL                0x0
#define FTS_TRUE                0x01
#define FTS_FALSE               0x0

#define I2C_CTPM_ADDRESS       0x70


static void ft5x0x_ts_release(void)
{
	input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);
	input_sync(tsdata->input);
}


//read touch point information
static int ft5x0x_read_data(void)
{
//	printk("%s\n", __func__);
	struct ts_event *event = &tsdata->event;
	u8 buf[CFG_POINT_READ_BUF] = {0};
	int ret = -1;
	int i;

	//ret = ft5x0x_i2c_rxdata(buf, CFG_POINT_READ_BUF);
	ret = i2c_master_recv(tsdata->client, buf, CFG_POINT_READ_BUF);
    if (ret < 0) {
		printk("%s read_data i2c_rxdata failed: %d\n", __func__, ret);
		return ret;
	}
	memset(event, 0, sizeof(struct ts_event));
	event->touch_point = buf[2] & 0x07; 

    if (event->touch_point > CFG_MAX_TOUCH_POINTS)
    {
        event->touch_point = CFG_MAX_TOUCH_POINTS;
    }

    for (i = 0; i < event->touch_point; i++)
    {
        event->au16_x[i] = (s16)(buf[3 + 6*i] & 0x0F)<<8 | (s16)buf[4 + 6*i];
        event->au16_y[i] = (s16)(buf[5 + 6*i] & 0x0F)<<8 | (s16)buf[6 + 6*i];
        event->au8_touch_event[i] = buf[0x3 + 6*i] >> 6;
        event->au8_finger_id[i] = (buf[5 + 6*i])>>4;
    }

    event->pressure = 200;

    return 0;
}

/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/


#if CFG_SUPPORT_TOUCH_KEY
int ft5x0x_touch_key_process(struct input_dev *dev, int x, int y, int touch_event)
{
    int i;
    int key_id;
    
    if ( y < 517&&y > 497)
    {
        key_id = 1;
    }
    else if ( y < 367&&y > 347)
    {
        key_id = 0;
    }
    
    else if ( y < 217&&y > 197)
    {
        key_id = 2;
    }  
    else if (y < 67&&y > 47)
    {
        key_id = 3;
    }
    else
    {
        key_id = 0xf;
    }
    
    for(i = 0; i <CFG_NUMOFKEYS; i++ )
    {
        if(tsp_keystatus[i])
        {
            input_report_key(dev, tsp_keycodes[i], 0);
      
            printk("[FTS] %s key is release. Keycode : %d\n", tsp_keyname[i], tsp_keycodes[i]);

            tsp_keystatus[i] = KEY_RELEASE;
        }
        else if( key_id == i )
        {
            if( touch_event == 0)                                  // detect
            {
                input_report_key(dev, tsp_keycodes[i], 1);
                printk( "[FTS] %s key is pressed. Keycode : %d\n", tsp_keyname[i], tsp_keycodes[i]);
                tsp_keystatus[i] = KEY_PRESS;
            }
        }
    }
    return 0;
    
}    
#endif

static void ft5x0x_report_value(void)
{
	struct ts_event *event = &tsdata->event;
	int i;
	int x1,y1,x2,y2;

	for (i  = 0; i < event->touch_point; i++)
	{
	    if (event->au16_x[i] < SCREEN_MAX_X && event->au16_y[i] < SCREEN_MAX_Y)
	    // LCD view area
	    {
	        input_report_abs(tsdata->input, ABS_MT_POSITION_X, event->au16_x[i]);
    		input_report_abs(tsdata->input, ABS_MT_POSITION_Y, event->au16_y[i]);
    		input_report_abs(tsdata->input, ABS_MT_WIDTH_MAJOR, 1);
    		input_report_abs(tsdata->input, ABS_MT_TRACKING_ID, event->au8_finger_id[i]);
	//	printk("x[%d]:%d,y[%d]:%d\n", i, event->au16_y[i]*800/600, i, event->au16_y[i]*600/800);
//		printk("x[%d]:%d,y[%d]:%d\n", i, event->au16_x[i], i, event->au16_y[i]);
    		if (event->au8_touch_event[i]== 0 || event->au8_touch_event[i] == 2)
    		{
    		    input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, event->pressure);
    		}
    		else
    		{
    		    input_report_abs(tsdata->input, ABS_MT_TOUCH_MAJOR, 0);
    		}
	    }
	    else //maybe the touch key area
	    {
#if CFG_SUPPORT_TOUCH_KEY
            if (event->au16_x[i] >= SCREEN_MAX_X)
            {
                ft5x0x_touch_key_process(tsdata->input, event->au16_x[i], event->au16_y[i], event->au8_touch_event[i]);
            }
#endif
	    }
	    
			
		input_mt_sync(tsdata->input);
	}
	input_sync(tsdata->input);

    if (event->touch_point == 0) {
        ft5x0x_ts_release();
        return ; 
    }


}	/*end ft5x0x_report_value*/


/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
//	printk("%s\n", __func__);
	int ret = -1;

	ret = ft5x0x_read_data();	
	if (ret == 0) {	
		ft5x0x_report_value();
	}
	
	enable_irq(tsdata->irq);
}
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
	//printk("%s\n", __func__);
//	struct tsdata_data *tsdata = dev_id;


	disable_irq_nosync(tsdata->irq);
//	if (!work_pending(&tsdata->pen_event_work)) {
//		queue_work(tsdata->ts_workqueue, &tsdata->pen_event_work);
//	}
	queue_work(ts_workqueue, &tsdata->work.work);

	return IRQ_HANDLED;
}

static int ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
//	struct tsdata_data *tsdata;
	printk("%s\n", __func__);
	struct input_dev *input;
	int err = 0;
	unsigned char uc_reg_value; 
	unsigned char buf_recv[4] = {0};
	err = i2c_master_recv(client, buf_recv, 4);
	if(err < 0)
	{
		printk("err:%d\n", err);
		goto exit_alloc_data_failed;
	}
//	printk("err:%d\n", err);
#if CFG_SUPPORT_TOUCH_KEY
    int i;
#endif

	
	printk("[FTS] tsdata_probe, driver version is %s.\n", CFG_FTS_CTP_DRIVER_VERSION);
	
	/*
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}
	*/

	tsdata = kzalloc(sizeof(*tsdata), GFP_KERNEL);
	if (!tsdata)	{
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}


//	this_client = client;
//	i2c_set_clientdata(client, tsdata);

	INIT_DELAYED_WORK(&tsdata->work, ft5x0x_ts_pen_irq_work);
	
	ts_workqueue = create_singlethread_workqueue("ft5x0x_ts");
	if (!ts_workqueue) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}

//	disable_irq(client->irq);

	input = input_allocate_device();
	if (!input) {
		err = -ENOMEM;
		dev_err(&client->dev, "failed to allocate input device\n");
		goto exit_input_alloc_failed;
	}
	
	tsdata->client = client;
	tsdata->input = input;
	tsdata->irq = client->irq;

	set_bit(ABS_MT_TOUCH_MAJOR, input->absbit);
	set_bit(ABS_MT_POSITION_X, input->absbit);
	set_bit(ABS_MT_POSITION_Y, input->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input->absbit);

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);
    	input_set_abs_params(input, ABS_MT_TRACKING_ID, 0, 5, 0, 0);


	set_bit(EV_KEY, input->evbit);
	set_bit(EV_ABS, input->evbit);

#if CFG_SUPPORT_TOUCH_KEY
    //setup key code area
    set_bit(EV_SYN, input->evbit);
    set_bit(BTN_TOUCH, input->keybit);
    input->keycode = tsp_keycodes;
    for(i = 0; i < CFG_NUMOFKEYS; i++)
    {
        input_set_capability(input, EV_KEY, ((int*)input->keycode)[i]);
        tsp_keystatus[i] = KEY_RELEASE;
    }
#endif

	err = request_irq(client->irq, ft5x0x_ts_interrupt, IRQF_TRIGGER_FALLING, "ft5x0x_ts", &tsdata->irq);
	if (err < 0) {
		dev_err(&client->dev, "ft5x0x_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

	input->name		= "focal-touch";		//dev_name(&client->dev)
	err = input_register_device(input);
	if (err) {
		dev_err(&client->dev,
		"tsdata_probe: failed to register input device: %s\n",
		dev_name(&client->dev));
		goto exit_input_register_device_failed;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	printk("==register_early_suspend =\n");
	tsdata->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	tsdata->early_suspend.suspend = tsdata_suspend;
	tsdata->early_suspend.resume	= tsdata_resume;
	register_early_suspend(&tsdata->early_suspend);
#endif

    msleep(150);  //make sure CTP already finish startup process
    
    //get some register information
    /*
    uc_reg_value = ft5x0x_read_fw_ver();
    printk("[FTS] Firmware version = 0x%x\n", uc_reg_value);
    ft5x0x_read_reg(FT5X0X_REG_PERIODACTIVE, &uc_reg_value);
    printk("[FTS] report rate is %dHz.\n", uc_reg_value * 10);
    ft5x0x_read_reg(FT5X0X_REG_THGROUP, &uc_reg_value);
    printk("[FTS] touch threshold is %d.\n", uc_reg_value * 4);
    */

    /*
#if CFG_SUPPORT_AUTO_UPG
    fts_ctpm_auto_upg();
#endif    

#if CFG_SUPPORT_UPDATE_PROJECT_SETTING
    fts_ctpm_update_project_setting();
#endif
*/

    //enable_irq(client->irq);

	printk("[FTS] ==probe over =\n");
    return 0;

exit_input_register_device_failed:
	input_free_device(input);
exit_input_alloc_failed:
	free_irq(client->irq, client);
//	free_irq(IRQ_EINT(6), tsdata);
exit_irq_request_failed:
//exit_platform_data_null:
//	cancel_work_sync(&tsdata->pen_event_work);
	destroy_workqueue(ts_workqueue);
exit_create_singlethread:
	printk("==singlethread error =\n");
	i2c_set_clientdata(client, NULL);
	kfree(tsdata);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}
/***********************************************************************************************
Name	:	 

Input	:	
                     

Output	:	

function	:	

***********************************************************************************************/
static int __devexit ft5x0x_ts_remove(struct i2c_client *client)
{
//	struct ft5x0x_ts_data *ft5x0x_ts;
	printk("==ft5x0x_ts_remove=\n");
//	ft5x0x_ts = i2c_get_clientdata(client);
//	unregister_early_suspend(&ft5x0x_ts->early_suspend);
//	free_irq(client->irq, ft5x0x_ts);
	free_irq(client->irq, client);
//	free_irq(IRQ_EINT(6), ft5x0x_ts);
//	input_unregister_device(ft5x0x_ts->input);
	input_unregister_device(tsdata->input);
	kfree(tsdata);
//	cancel_work_sync(&ft5x0x_ts->pen_event_work);
//	destroy_workqueue(ft5x0x_ts->ts_workqueue);
//	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct i2c_device_id ft5x0x_ts_id[] = {
	{ "focal-touch", 0 },
	{ }
};


MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver ft5x0x_ts_driver = {
	.probe		= ft5x0x_ts_probe,
	.remove		= __devexit_p(ft5x0x_ts_remove),
	.id_table	= ft5x0x_ts_id,
	.driver	= {
		.name	= "focal-touch",
		.owner	= THIS_MODULE,
	},
};

static int __init ft5x0x_ts_init(void)
{
	int ret;
	printk("==ft5x0x_ts_init==\n");
	ret = i2c_add_driver(&ft5x0x_ts_driver);
	printk("ret=%d\n",ret);
	return ret;
//	return i2c_add_driver(&ft5x0x_ts_driver);
}

static void __exit ft5x0x_ts_exit(void)
{
	printk("==ft5x0x_ts_exit==\n");
	i2c_del_driver(&ft5x0x_ts_driver);
}

module_init(ft5x0x_ts_init);
module_exit(ft5x0x_ts_exit);

MODULE_AUTHOR("<wenfs@Focaltech-systems.com>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");
