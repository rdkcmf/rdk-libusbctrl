/*
 * If not stated otherwise in this file or this component's Licenses.txt file the
 * following copyright and licenses apply:
 *
 * Copyright 2016 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/
#include "libudev.h"
#include "usbctrl.h"
#include <iostream>
#include <stdio.h>
#include <list>
#include <sys/select.h>
#include "pthread.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

//#define ENABLE_DEBUG 1
#define LOG(level, text, ...) do {\
    printf("%s[%d] - %s: " text, __FUNCTION__, __LINE__, level, ##__VA_ARGS__);}while(0);

#define ERROR(text, ...) do {\
    printf("%s[%d] - %s: " text, __FUNCTION__, __LINE__, "ERROR", ##__VA_ARGS__);}while(0);
#define INFO(text, ...) do {\
    printf("%s[%d] - %s: " text, __FUNCTION__, __LINE__, "INFO", ##__VA_ARGS__);}while(0);

#ifdef ENABLE_DEBUG
#define DEBUG(text, ...) do {\
    printf("%s[%d] - %s: " text, __FUNCTION__, __LINE__, "DEBUG", ##__VA_ARGS__);}while(0);
#else
#define DEBUG(text, ...)
#endif

#define REPORT_IF_UNEQUAL(lhs, rhs) do {\
    if((lhs) != (rhs)) ERROR("Unexpected error!\n");}while(0);

#define UDEV_ADD_EVENT "add"
#define UDEV_REMOVE_EVENT "remove"
static const suseconds_t MONITOR_TIMEOUT_USECS = 250000;
static const int PIPE_READ_FD = 0;
static const int PIPE_WRITE_FD = 1;
static const int CONTROL_MESSAGE_SIZE = 4;

const char * supported_property_list[] = 
	{
		"manufacturer",
		"product",
		"idProduct",
		"idVendor",
		"serial",
		"bInterfaceClass",
		"bInterfaceSubClass"
	};

class device_manager
{
	public :
	class device_record
	{
		private:
		int m_identifier;
		struct udev_device *m_device;
		const char* m_devnode;

		public:
		device_record(int identifier, struct udev_device * device, const char* devnode) : 
			m_identifier(identifier), m_device(device), m_devnode(devnode)
		{
			DEBUG("adding device 0x%x, %s\n", (unsigned int)m_device, m_devnode);
		}
		~device_record()
		{
			/* The udev_device entry needs to be unreffed when the device is removed.*/
			DEBUG("Unreffing device 0x%x, %s\n", (unsigned int)m_device, m_devnode);
			udev_device_unref(m_device);
		}
		inline struct udev_device* get_device() {return m_device;}
		inline int get_identifier() {return m_identifier;}
		inline const char * get_devnode() {return m_devnode;}

	};

	private:
	std::list<device_record *> m_device_records;
	pthread_mutex_t m_mutex;
	rusbCtrl_devCallback_t m_callback;
	void * m_callback_data;
	struct udev *m_udev_context;
	int m_last_used_identifier;
	bool m_enable_monitoring;
	pthread_t m_monitor_thread;
	struct udev_monitor * m_monitor;
	int m_control_pipe[2];

	public:
	device_manager() : m_enable_monitoring(false), m_callback(NULL), m_last_used_identifier(0), m_monitor_thread(0)
	{
		pthread_mutexattr_t mutex_attribute;
		REPORT_IF_UNEQUAL(0, pthread_mutexattr_init(&mutex_attribute));
		REPORT_IF_UNEQUAL(0, pthread_mutexattr_settype(&mutex_attribute, PTHREAD_MUTEX_RECURSIVE));
		REPORT_IF_UNEQUAL(0, pthread_mutex_init(&m_mutex, &mutex_attribute));

		INFO("Creating new device manager object.\n");
		m_udev_context = udev_new();
		if(NULL == m_udev_context)
		{
			ERROR("Critical error! udev_new() failed!\n");
		}
		else
		{
			INFO("Successfully created device manager object 0x%x with udev context 0x%x.\n",
				(unsigned int)this, (unsigned int)m_udev_context);
		}

		/* Set up event monitoring. */		
		m_enable_monitoring = true;
		m_monitor = udev_monitor_new_from_netlink(m_udev_context, "udev");
		if(NULL == m_monitor)
		{
			ERROR("Critical error! Could not create monitor!\n");
			return;
		}

		do
		{
			if(0 != udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "usb", "usb_device"))
			{
				ERROR("Critical error! Could not add filters to udev monitor.\n");
				break;
			}

			if(0 != udev_monitor_enable_receiving(m_monitor))
			{
				ERROR("Critical error! Could not enable monitoring!\n");
				break;
			}
			if(0 != pipe(m_control_pipe))
			{
				ERROR("Critical error! Could not create pipe.\n");
				m_control_pipe[PIPE_READ_FD] = 0;
				m_control_pipe[PIPE_WRITE_FD] = 0;
				break;
			}
			if(0 != pthread_create(&m_monitor_thread, NULL, device_manager::monitor_thread_wrapper, (void *)this))
			{
				ERROR("Critical error! Could not launch monitor thread!\n");
				m_monitor_thread = 0;
			}
		}while(0);
		INFO("Done.\n");
	}

	~device_manager()
	{	
		INFO("Stopping monitor thread.\n");
		m_enable_monitoring = false;
		/* Closing the below fd will unblock monitor thread waiting on select().*/
		if(0 != m_control_pipe[PIPE_WRITE_FD])
		{
			close(m_control_pipe[PIPE_WRITE_FD]);
		}

		if(0 != m_monitor_thread)
		{
			if(0 != pthread_join(m_monitor_thread, NULL))
			{
				ERROR("Error. Monitor thread did not join.\n");
			}
		}

		if(0 != m_control_pipe[PIPE_READ_FD])
		{
			close(m_control_pipe[PIPE_READ_FD]);
		}

		reset_device_records();

		if(NULL != m_monitor)
		{
			udev_monitor_unref(m_monitor);
		}

		INFO("Destroying device manager object.\n");
		udev_unref(m_udev_context);
		pthread_mutex_destroy(&m_mutex);
		INFO("Done.\n");
	}
	static void * monitor_thread_wrapper(void* data)
	{
		device_manager *obj = (device_manager *)data;
		obj->monitor_for_changes();
	}
	rusbCtrl_result_t init()
	{
		INFO("Enter.\n");
		enumerate_connected_devices();
		INFO("Done.\n");
		return RUSBCTRL_SUCCESS;
	}

	rusbCtrl_result_t term()
	{
		INFO("Enter\n");
		INFO("Clearing device records.\n");
		REPORT_IF_UNEQUAL(0, pthread_mutex_lock(&m_mutex));
		reset_device_records();	
		m_callback = NULL;
		m_callback_data = NULL;
		REPORT_IF_UNEQUAL(0, pthread_mutex_unlock(&m_mutex));
		INFO("Done.\n");
		return RUSBCTRL_SUCCESS;
	}
	

	char * get_property(int identifier, const char *key) //needs lock
	{
		/* Find matching entry in the record*/
		std::list<device_record *>::iterator iter;
		struct udev_device *device;
		for(iter = m_device_records.begin(); iter != m_device_records.end(); iter++)
		{
			if(identifier == (*iter)->get_identifier())
			{
				device = (*iter)->get_device();
				break;
			}
		}
		if(iter != m_device_records.end())
		{
			DEBUG("Found record with identifer 0x%x. Querying...\n", identifier);
			const char * value = udev_device_get_sysattr_value(device, key);
			if(NULL == value)
			{
				ERROR("Could not find property %s.\n", key);
				return NULL;
			}
			else
			{
				char * user_buffer = (char*)malloc(strlen(value)); //Will be freed by user
				strncpy(user_buffer, value, strlen(value));
				return user_buffer;
			}
		}
		ERROR("Found no record for device with id 0x%x\n", identifier);
		return NULL;
	}

	int register_callback(rusbCtrl_devCallback_t callback, void* callback_data, int ** device_list, int * device_list_size)
	{
		//Note: device_list_size stands for the number of entries in the list, not the actual bytes
		REPORT_IF_UNEQUAL(0, pthread_mutex_lock(&m_mutex));	
		m_callback = callback;
		m_callback_data = callback_data;
		if((NULL != device_list) && (NULL != device_list_size))
		{
			get_connected_devices_list(device_list, device_list_size);	
		}
		else
		{
			ERROR("Empty pointers provided. Won't supply connected devices.\n");
		}
		REPORT_IF_UNEQUAL(0, pthread_mutex_unlock(&m_mutex));	
		INFO("Success!\n");
		return RUSBCTRL_SUCCESS;
	}
	
	void process_control_event()
	{
		int message = 0;

		if(0 == read(m_control_pipe[PIPE_READ_FD], (void *)&message, CONTROL_MESSAGE_SIZE))
		{
			/* Until further messages and use cases are defined, mere EOF is the trigger 
			 * for a shutdown.*/
			INFO("Detected EOF. Calling for shutdown.\n");
			m_enable_monitoring = false;
		}
	}

	void monitor_for_changes()
	{
		INFO("monitor thread launched.\n");
		int control_fd = m_control_pipe[PIPE_READ_FD];
		int monitor_fd = udev_monitor_get_fd(m_monitor);
		if(monitor_fd <= 0)
		{
			ERROR("Critical error! Could not get udev monitor fd.\n");
			return;
		}
		int max_fd = (monitor_fd > control_fd ? monitor_fd : control_fd);

		while(true == m_enable_monitoring)
		{
			fd_set monitor_fd_set;
			FD_ZERO(&monitor_fd_set);
			FD_SET(monitor_fd, &monitor_fd_set);
			FD_SET(control_fd, &monitor_fd_set);

			int ret = select((max_fd + 1), &monitor_fd_set, NULL, NULL, NULL);
			DEBUG("Unblocking now. ret is 0x%x\n", ret);
			if(0 == ret)
			{
				ERROR("select() returned 0.\n");
				m_enable_monitoring = false;
				break;
			}
			else if(0 < ret)
			{
				//Some activity was detected. Process event further.
				if(0 != FD_ISSET(control_fd, &monitor_fd_set))
				{
					process_control_event();			
				}
				if(0 != FD_ISSET(monitor_fd, &monitor_fd_set))
				{
					process_udev_monitor_event();
				}
			}
			else
			{
				m_enable_monitoring = false;
				ERROR("Error polling monitor FD!\n");
			}

		}
		close(monitor_fd);
		INFO("Monitor thread shutting down.\n");
	}

	private:

	void reset_device_records() //needs lock
	{
		if(0 != m_device_records.size())
		{
			std::list<device_record *>::iterator iter;
			for(iter = m_device_records.begin(); iter != m_device_records.end(); iter++)
			{
				delete(*iter);
			}
			m_device_records.clear();
		}
		INFO("Done.\n");
	}

	void get_connected_devices_list(int ** device_list, int * device_list_size) //needs lock
	{
		//Note: device_list_size stands for the number of entries in the list, not the actual bytes
		*device_list_size = m_device_records.size();
		if(0 != *device_list_size)
		{
			/* It's application's responsibility to free this buffer.*/
			int * buffer = (int *)malloc(*device_list_size * sizeof(int));
			std::list<device_record *>::iterator iter;
			int i = 0;
			for(iter = m_device_records.begin(); iter != m_device_records.end(); iter++)
			{
				buffer[i++] = (*iter)->get_identifier();
			}
			*device_list = buffer;
		}

	}

	rusbCtrl_result_t enumerate_connected_devices()
	{
		rusbCtrl_result_t result = RUSBCTRL_SUCCESS;
		REPORT_IF_UNEQUAL(0, pthread_mutex_lock(&m_mutex));	
		reset_device_records();	
		struct udev_enumerate *enumerator = udev_enumerate_new(m_udev_context);
		if(NULL == enumerator)
		{
			REPORT_IF_UNEQUAL(0, pthread_mutex_unlock(&m_mutex));
			ERROR("Could not create udev enumerator!\n");
			return RUSBCTRL_FAILURE;
		}
		
		do
		{
			if(0 != udev_enumerate_add_match_property(enumerator, "DEVTYPE", "usb_device"))
			{
				ERROR("Couldn't add property to match.\n");
				result = RUSBCTRL_FAILURE;
				break;
			}
			if(0 != udev_enumerate_scan_devices(enumerator))
			{
				ERROR("Couldn't scan devices.\n");
				result = RUSBCTRL_FAILURE;
				break;
			}

			struct udev_list_entry *device_list_head = udev_enumerate_get_list_entry(enumerator);
			struct udev_list_entry *device_list_iterator = NULL;
			udev_list_entry_foreach(device_list_iterator, device_list_head)
			{
				const char * sys_path = udev_list_entry_get_name(device_list_iterator);
				struct udev_device *device = udev_device_new_from_syspath(m_udev_context, sys_path);
				INFO("Detected device [syspath: %s, udev_device prt: 0x%x]\n", sys_path, (unsigned int)device);
				if(NULL != device)
				{
					int identifier; 
					add_device_to_records(device, identifier);
				}
			}
		}while(0);
		REPORT_IF_UNEQUAL(0, pthread_mutex_unlock(&m_mutex));
		udev_enumerate_unref(enumerator);
		return result;
	}

	bool add_device_to_records(struct udev_device *device, int &identifier) //needs lock
	{
		/* Create record and push it into the list. */
		identifier = get_new_identifier();
		INFO("Adding device 0x%x to records. Identifier is 0x%x\n", (unsigned int)device, identifier);
		device_record *temp(new device_record(identifier, device, udev_device_get_devnode(device)));
		m_device_records.push_back(temp);
		print_device_properties(device);
		return true;
	}

	bool remove_device_from_records(struct udev_device *device, int &identifier) //needs lock
	{
		identifier = -1;
		DEBUG("Removing device 0%x from records.\n", (unsigned int)device);
		const char * devnode = udev_device_get_devnode(device);
		if(NULL ==  devnode)
		{
			ERROR("Invalid devnode for incoming data.\n");
			return false;
		}
		/* Find matching entry in the record*/
		std::list<device_record *>::iterator iter;
		for(iter = m_device_records.begin(); iter != m_device_records.end(); iter++)
		{
			if(0 == strncmp(devnode, (*iter)->get_devnode(), strlen(devnode)))
			{
				identifier = (*iter)->get_identifier();
				break;
			}
		}
		if(iter != m_device_records.end())
		{
			INFO("Found record with identifer 0x%x. Removing it.\n", identifier);
			delete(*iter);
			m_device_records.erase(iter);
			return true;
		}
		ERROR("Found no record for device\n");
		return false;
	}
	void print_device_properties(struct udev_device * device) //needs lock
	{
		INFO("USB device Node Path: %s\n", udev_device_get_devnode(device));
#if 0
		udev_list_entry *device_attr_list = udev_device_get_sysattr_list_entry(device);
		udev_list_entry *current_attr = NULL;
		const char *key = NULL;
		udev_list_entry_foreach(current_attr, device_attr_list)
		{
			key = udev_list_entry_get_name(current_attr);
			DEBUG("key: %s, value: %s\n", key, udev_device_get_sysattr_value(device, key));
		}
#endif // Disabled because udev_device_get_sysattr_list_entry() is not supported on certain platforms.
	}
	
	void process_udev_monitor_event()
	{
		struct udev_device *device = udev_monitor_receive_device(m_monitor);
		if(NULL == device)
		{
			ERROR("udev_monitor_receive_device failed!\n");
			return;
		}

		const char* action = udev_device_get_action(device);

		if(0 == strncmp(action, UDEV_ADD_EVENT, strlen(UDEV_ADD_EVENT)))
		{
			//Process 'add' event.
			int identifier;
			bool result;
			{
				REPORT_IF_UNEQUAL(0, pthread_mutex_lock(&m_mutex));	
				result = add_device_to_records(device, identifier);
				/*Note: the object "device" is not unreffed here. Instead, the ownership has now been passed to
				 * m_device_records list. "device" will be automatically unreffed when its device_record is destroyed.*/
				REPORT_IF_UNEQUAL(0, pthread_mutex_unlock(&m_mutex));
			}
			if((result) && (m_callback))
			{
				m_callback(identifier, 1, m_callback_data);
			}

		}
		else if(0 == strncmp(action, UDEV_REMOVE_EVENT, strlen(UDEV_REMOVE_EVENT)))
		{
			//Process 'remove' event.
			int identifier;
			bool result;
			{
				REPORT_IF_UNEQUAL(0, pthread_mutex_lock(&m_mutex));
				result = remove_device_from_records(device, identifier);
				REPORT_IF_UNEQUAL(0, pthread_mutex_unlock(&m_mutex));
			}
			udev_device_unref(device);
			if((result) && (m_callback))
			{
				m_callback(identifier, 0, m_callback_data);
			}
		}
		else
		{
			udev_device_unref(device);
		}
	}
	int get_new_identifier() //needs lock
	{
		//TODO: handle roll-over
		m_last_used_identifier++;
		return m_last_used_identifier;
	}
};


static device_manager manager;
int rusbCtrl_init(void)
{
	rusbCtrl_result_t result = manager.init(); 
	return result;

}
int rusbCtrl_term()
{
	rusbCtrl_result_t result = manager.term();
	return result;
}
int rusbCtrl_registerCallback(rusbCtrl_devCallback_t cb, void *cbData, int **devList, int *devListNumEntries)
{
	return manager.register_callback(cb, cbData, devList, devListNumEntries);
}
char *rusbCtrl_getProperty(int devId, const char *propertyName)
{
	return manager.get_property(devId, propertyName);
}

