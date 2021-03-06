#include "Game.h"
#include "World.h"
#include "SFML/Window/Keyboard.hpp"
#include "SFML/System/Sleep.hpp"
#include "Message.h"
#include "Messenger.h"
#include "GameObject.h"
#include <cassert>
#include <iostream>
#include "Collidable.h"

Game::Game(sf::RenderWindow* _RenderWindow)
{
	InitializeGame(_RenderWindow);

	AttachToMessenger(GetMessenger("KeyEvents"));
	AttachToMessenger(GetMessenger("GlobalEvents"));
}

Game::~Game()
{
	mActive = false;
	mCurrentWorld.reset();

	while (mPhysicsThreadActive || mRenderingThreadActive || mControllerThreadActive || mMessagingThreadActive)
	{
		sf::sleep(sf::milliseconds(100));
	}

	if (mPhysicsThread.joinable())
	{
		mPhysicsThread.join();
	}

	if (mControllerThread.joinable())
	{
		mControllerThread.join();
	}

	if (mRenderingThread.joinable())
	{
		mRenderingThread.join();
	}

	if (mMessagingThread.joinable())
	{
		mMessagingThread.join();
	}

	mRenderWindow->close();
}

/// <summary> 
/// Primary thread for ticking physics.
/// </summary>
void Game::PhysicsThread()
{
	mPhysicsThreadActive = true;

	while (mActive && mPhysicsThreadActive)
	{
		while (mBlockAllThreads || mCurrentWorld == nullptr)
		{
			sf::sleep(sf::milliseconds(1));
		}

		mPhysicTicking = true;

		sf::Time CurrentTime = mClock.getElapsedTime();
		sf::Time Elapsedtime = CurrentTime - mLastPhysicsTime;
		mLastPhysicsTime = CurrentTime;

		// Tick Physics
		mCurrentWorld.get()->Tick(Elapsedtime);

		// -TODO- Move collision detection to its own thread.
		// -TODO- Clean up implementation.
		/*for (unsigned int i = 0; i < mObjectsToRender.size(); i++)
		{
			for (unsigned int j = 0; j < mObjectsToRender[i].size(); j++)
			{
				Collidable* CollidableObject1;
				if ((CollidableObject1 = dynamic_cast<Collidable*>(mObjectsToRender[i][j])) != nullptr)
				{
					for (unsigned int k = i; k < mObjectsToRender.size(); k++)
					{
						for (unsigned int l = (k == i) ? j + 1 : 0; l < mObjectsToRender[k].size(); l++)
						{
							Collidable* CollidableObject2;
							if ((CollidableObject2 = dynamic_cast<Collidable*>(mObjectsToRender[k][l])) != nullptr &&
								(CollidableObject1 = dynamic_cast<Collidable*>(mObjectsToRender[i][j])) != nullptr)
							{
								CollidableObject1->CheckCollision(CollidableObject2);
							}
						}
					}
				}
			}
		}*/

		// Queue tick message
		GetMessenger("Collision")->ReceiveMessage(std::make_unique<Message>(Message(MESSAGE_TYPE_EVENT, TICK_EVENT)));

		mPhysicTicking = false;

		// Tick no faster than once every 10ms
		if (Elapsedtime.asMilliseconds() < 10)
		{
			sf::sleep(sf::milliseconds(10 - Elapsedtime.asMilliseconds()));
		}
	}

	mPhysicsThreadActive = false;
	mActive = false;
}

/// <summary> 
/// Primary thread for ticking controllers.
/// </summary>
void Game::ControllerThread()
{
	mControllerThreadActive = true;

	while (mActive && mControllerThreadActive)
	{
		while (mBlockAllThreads || mCurrentWorld == nullptr)
		{
			sf::sleep(sf::milliseconds(1));
		}

		mControllerTicking = true;

		CheckControls();

		sf::Time CurrentTime = mClock.getElapsedTime();
		sf::Time Elapsedtime = CurrentTime - mLastControllerTime;
		mLastControllerTime = CurrentTime;

		// Tick the controller
		mCurrentWorld.get()->ControllerTick(Elapsedtime);

		mControllerTicking = false;

		// Tick no faster than once every 50ms
		if (Elapsedtime.asMilliseconds() < 50)
		{
			sf::sleep(sf::milliseconds(50 - Elapsedtime.asMilliseconds()));
		}
	}

	mControllerThreadActive = false;
	mActive = false;
}

/// <summary> 
/// Primary rendering thread.
/// </summary>
void Game::RenderingThread()
{
	mRenderingThreadActive = true;

	// Pick up the window inactive so no other threads can handle it.
	mRenderWindow->setActive(true);

	while (mActive && mRenderingThreadActive)
	{
		// Always wait for physics to complete at least one pass
		while (mBlockAllThreads)
		{
			sf::sleep(sf::milliseconds(1));
		}

		mRendering = true;

		sf::Time CurrentTime = mClock.getElapsedTime();
		sf::Time Elapsedtime = CurrentTime - mLastRenderTime;
		mLastRenderTime = CurrentTime;

		// Clear screen with black
		mRenderWindow->clear(sf::Color(0,0,0,255));

		// Tick Rendering of all objects needing rendering
		for (std::vector<GameObject*> goVector : mObjectsToRender)
		{
			for (GameObject* go : goVector)
			{
				go->RenderTick(mRenderWindow);
			}
		}

		// Throw the buffer to display!
		mRenderWindow->display();

		mRendering = false;

		// Tick no faster than once every 10ms
		if (Elapsedtime.asMilliseconds() < 10)
		{
			sf::sleep(sf::milliseconds(10 - Elapsedtime.asMilliseconds()));
		}
	}

	mRenderingThreadActive = false;
	mActive = false;
}

/// <summary> 
/// Primary Messaging thread.
/// </summary>
void Game::MessagingThread()
{
	mMessagingThreadActive = true;

	while (mActive && mMessagingThreadActive)
	{
		while (mBlockAllThreads)
		{
			sf::sleep(sf::milliseconds(1));
		}

		mMessengerTicking = true;

		sf::Time CurrentTime = mClock.getElapsedTime();
		sf::Time Elapsedtime = CurrentTime - mLastMessagingTime;
		mLastMessagingTime = CurrentTime;

		// -TODO- Weigh whether these warrant two independant threads.
		// While the message queue is not empty, give messages to messengers.
		while (mMessageQueue.size() > 0)
		{
			GetMessenger(mMessageQueue.front().first).get()->ReceiveMessage(std::move(mMessageQueue.front().second));
			mMessageQueue.pop();
		}

		// For all messengers, tick.
		for (std::pair<std::string, std::shared_ptr<Messenger>> it : mMessengers)
		{
			it.second.get()->TickMessenger();
		}

		mMessengerTicking = false;

		// Tick no faster than once every 50ms
		if (Elapsedtime.asMilliseconds() < 50)
		{
			sf::sleep(sf::milliseconds(50 - Elapsedtime.asMilliseconds()));
		}
	}

	mMessagingThreadActive = false;
	mActive = false;
}

/// <summary> 
/// Interprets messages passed.
/// </summary>
void Game::ReadMessage(Message* _Message)
{
	switch (_Message->GetMessageType())
	{
		case MESSAGE_TYPE_INVALID:
			return;
			break;
		case MESSAGE_TYPE_INPUT:
			CheckControls(_Message->GetMessageDouble());
			break;
		case MESSAGE_TYPE_DOUBLE:
			if (_Message->GetMessageDouble() == RESTART_LEVEL)
			{
				std::shared_ptr<World> WorldCopy = mCurrentWorld;

				mCurrentWorld = GetNewWorld();
			}
			break;
		case MESSAGE_TYPE_STRING:

			break;
		case MESSAGE_TYPE_QUIT:
			std::cout << _Message->GetMessageString(); // -TODO- Implement a logging file so this can be written to it on exit.
			EndGame(); // Quit
			break;
		default:

			break;
	}
}

/// <summary> 
/// Checks controls, should only be used with overrides, as the global level shouldn't be
/// looking for any specific key inputs.
/// </summary>
void Game::CheckControls(int _OverrideControl)
{
	// -TODO- Implment any GLOBAL controls here.
}

/// <summary> 
/// Returns a pointer to the messenger requested
/// </summary>
std::shared_ptr<Messenger> Game::GetMessenger(std::string _MessengerName)
{
	std::map<std::string, std::shared_ptr<Messenger>>::iterator Result = mMessengers.find(_MessengerName);

	if (Result == mMessengers.end())
	{
		mMessengers.emplace(std::make_pair(_MessengerName, std::make_shared<Messenger>()));
		Result = mMessengers.find(_MessengerName);
	}

	return Result->second;
}

void Game::QueueMessage(std::string _MessengerName, std::unique_ptr<Message> _Message)
{
	mMessageQueue.push(std::make_pair(_MessengerName, std::move(_Message)));
}

void Game::AddObjectToRenderer(GameObject* _GameObject, int _Layer)
{
	assert(_Layer < NUMBER_OF_LAYERS);

	mObjectsToRender[_Layer].push_back(_GameObject);

	// if object is collidable, add to collision messenger.
	if (dynamic_cast<Collidable*>(_GameObject) != nullptr)
	{
		_GameObject->AttachToMessenger(GetMessenger("Collision"));
	}

	_GameObject->mAddedToRenderer = true;
}

void Game::RemoveObjectFromRenderer(GameObject* _GameObject, int _Layer)
{
	assert(_Layer < NUMBER_OF_LAYERS);

	for (unsigned int i = 0; i < mObjectsToRender[_Layer].size(); i++)
	{
		if (_GameObject == mObjectsToRender[_Layer][i])
		{
			mObjectsToRender[_Layer].erase(mObjectsToRender[_Layer].begin()+i);
			return;
		}
	}
}

std::shared_ptr<World> Game::GetNewWorld()
{
	return std::make_shared<World>(this);
}

void Game::InitializeGame(sf::RenderWindow* _RenderWindow)
{
	mActive = true;
	mBlockAllThreads = false;
	mRenderWindow = _RenderWindow;
	mObjectsToRender.resize(NUMBER_OF_LAYERS);
	for (int i = 0; i < NUMBER_OF_LAYERS; i++)
	{
		mObjectsToRender[i].reserve(1000);
	}

	mClock.restart();

	mLastPhysicsTime = mClock.getElapsedTime();
	mLastControllerTime = mClock.getElapsedTime();
	mLastRenderTime = mClock.getElapsedTime();
	mLastMessagingTime = mClock.getElapsedTime();

	mPhysicsThread = std::thread(&Game::PhysicsThread, this);
	mControllerThread = std::thread(&Game::ControllerThread, this);
	mRenderingThread = std::thread(&Game::RenderingThread, this);
	mMessagingThread = std::thread(&Game::MessagingThread, this);
}


void Game::LockAllThreads()
{
	mBlockAllThreads = true;

	while ((int)mRendering + (int)mPhysicTicking + (int)mControllerTicking + (int)mMessengerTicking > 1)
	{
		std::this_thread::yield();
	}
}

void Game::UnlockAllThreads()
{
	mBlockAllThreads = false;
}