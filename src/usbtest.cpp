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
#include <iostream>
#include "usbctrl.h"
#include <unistd.h>
#include <stdlib.h>
#include <list>

static std::list<int> connected_device_ids;
void callback(int id, int connected, void *data)
{
	std::cout<<"Enter callback. Payload is "<<(char *)data<<std::endl;;
	if(connected)
	{
		const char * prop = rusbCtrl_getProperty(id, "product"); 
		std::cout<<"device "<<id<<" is connected\n";
		std::cout<<"Product: "<<prop<<std::endl;
        free((void *)prop);
		connected_device_ids.push_back(id);
	}
	else
	{
		connected_device_ids.remove(id);	
		std::cout<<"device "<<id<<" was removed\n";
	}
    
}





void print_menu(void)
{
	std::cout<<"\n--- libusbctrl test application menu ---\n";
	std::cout<<"1. rusbbCtrl_init()\n";
	std::cout<<"2. rusbbCtrl_term()\n";
	std::cout<<"3. rusbCtrl_registerCallback()\n";
	std::cout<<"4. rusbCtrl_getProperty()\n";
	std::cout<<"5. List hot-plugged dev_ids (not thread-safe).\n";
	std::cout<<"9. Quit.\n";
}

static std::string callback_payload = "Uninitialized";
void dump_connected_devices()
{
	//Note: This function is not thread-safe.
	std::cout<<"Hot-plugged device ids:\n";
	if(0 == connected_device_ids.size())
	{
		std::cout<<"No devices detected so far.\n";
	}
	else
	{
		std::list<int>::const_iterator iter;
		for(iter = connected_device_ids.begin(); iter != connected_device_ids.end(); iter++)
		{
			std::cout<<"["<<*iter<<"] ";
		}
		std::cout<<std::endl;
	}
}
void launcher()
{
	bool keep_running = true;
	while(keep_running)
	{
		int choice = 0;
		print_menu();
		std::cout<<"Enter command:\n";
		if(!(std::cin >> choice))
		{
			std::cout<<"Oops!\n";
			std::cin.clear();
			std::cin.ignore(10000, '\n');
			continue;
        }
		switch(choice)
		{
			case 1:
				rusbCtrl_init();
				break;
			case 2:
				rusbCtrl_term();
				connected_device_ids.clear();
				break;
			case 3:
				std::cout<<"Enter a string for callback_payload. This will be used to identify the callback you register.\n";
				if(!(std::cin >> callback_payload))
				{
					std::cout<<"Whoops! Bad input.\n";
					std::cin.clear();
					std::cin.ignore(10000, '\n');
				}
				else
				{
					int * device_list_ptr;
					int device_list_size;
					if(0 != rusbCtrl_registerCallback(callback, (void *)callback_payload.c_str(), &device_list_ptr, &device_list_size))
					{
						std::cout<<"Failed to register callback.\n";
					}
					else
					{
						std::cout<<"Registered callback with payload "<<callback_payload<<std::endl;
						for(int i = 0; i < device_list_size; i++)
						{
							connected_device_ids.push_back(device_list_ptr[i]);
						}
						free(device_list_ptr);
						dump_connected_devices();
					}
				}
				break;
			case 4:
				{
					std::cout<<"Enter devId(integer) and property(string) separated by a space.\n";
					std::cout<<"Some examples of properties: product serial manufacturer idProduct idVendor\n";
					int dev_id;
					std::string property;
					if(!(std::cin>>dev_id>>property))
					{
						std::cout<<"Whoops! Bad input.\n";
						std::cin.clear();
						std::cin.ignore(10000, '\n');
					}
					else
					{
						std::cout<<"Querying property "<<property<<" for dev_id "<<dev_id<<std::endl;
						const char * result = rusbCtrl_getProperty(dev_id, property.c_str());
						if(NULL == result)
						{
							std::cout<<"Query returned NULL!\n";
						}
						else
						{
							std::cout<<"Query returned "<<result<<std::endl;
							free((void *)result);
						}
					}
					break;
				}
			case 5:
				{
					dump_connected_devices();
					break;
				}
			case 9:
				keep_running = false;
				std::cout<<"Quitting.\n";
				break;

			default:
				std::cout<<"Unknown input!\n";
		}
	}
}

int main(int argc, char *argv[])
{
	launcher();
	return 0;
}
