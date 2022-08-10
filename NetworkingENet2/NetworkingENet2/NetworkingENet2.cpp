// NetworkingENet2.cpp : This file contains the 'main' function. Program execution begins and ends there.
// Simulates a guessing game in which up to 32 players guess a number that the server randomly chooses
//

#include <enet/enet.h>
#include <string>
#include <iostream>
#include <thread>
#include <windows.h>

using namespace std;

ENetHost* NetHost = nullptr;
ENetPeer* Peer = nullptr;

bool IsServer = false;

thread* PacketThread = nullptr;
thread* GuessThread = nullptr;
thread* QuitServerThread = nullptr;

bool quit = false;

int maximumGuessNumber = 100;
int guessNumber = 0;

string successMessage = "You guessed correctly! Congrats! Press 'enter' to exit.";
string failureMessage = "Incorrect! Keep trying.";
string otherPlayerWonMessage = "Another player guessed the number! Better luck next time. Press 'enter' to exit.";

enum PacketHeaderTypes
{
    PHT_Invalid = 0,
    PHT_IsDead,
    PHT_Position,
    PHT_Count
};

struct GamePacket
{
    GamePacket() {}
    PacketHeaderTypes Type = PHT_Invalid;
};

// Stop all threads
void TerminateThreads() 
{
    if (PacketThread)
    {
        PacketThread->join();
    }
    delete PacketThread;
    if (GuessThread)
    {
        GuessThread->join();
    }
    delete GuessThread;
    if (QuitServerThread)
    {
        QuitServerThread->join();
    }
    delete QuitServerThread;
}

// Allow user to quit server properly
void QuitServer() 
{
    while (!quit)
    {
        bool invalid = false;

        string guess;
        getline(cin, guess);

        if (guess == "quit")
        {
            quit = true;
            cout << "Closed server" << endl;
        }
    }
}

// Allow client to guess number
void GuessNumber() 
{
    while (!quit) 
    {
        bool invalid = false;

        string guess;
        getline(cin, guess);

        if (quit) 
        {
            cout << "Exited game" << endl;
        }

        else if (guess == "quit")
        {
            quit = true;
            cout << "Exited game" << endl;
        }

        else if (!guess.empty()) {
            for (unsigned int i = 0; i < guess.length(); i++)
            {
                if (!isdigit(guess[i]))
                {
                    cout << "Invalid input! Try again" << endl;
                    invalid = true;
                    break;
                }
            }

            if (!invalid)
            {
                ENetPacket* packet = enet_packet_create(guess.c_str(),
                    strlen(guess.c_str()) + 1,
                    ENET_PACKET_FLAG_RELIABLE);

                enet_host_broadcast(NetHost, 0, packet);

                enet_host_flush(NetHost);
            }
        }
    }
}

struct IsDeadPacket : public GamePacket
{
    IsDeadPacket()
    {
        Type = PHT_IsDead;
    }

    int playerId = 0;
    bool IsDead = false;
};

struct PositionPacket : public GamePacket
{
    PositionPacket()
    {
        Type = PHT_Position;
    }

    int playerId = 0;
    int x = 0;
    int y = 0;
};

// Can pass in a peer connection if wanting to limit
bool CreateServer()
{
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = 1234;
    NetHost = enet_host_create(&address /* the address to bind the server host to */,
        32      /* allow up to 32 clients and/or outgoing connections */,
        2      /* allow up to 2 channels to be used, 0 and 1 */,
        0      /* assume any amount of incoming bandwidth */,
        0      /* assume any amount of outgoing bandwidth */);

    return NetHost != nullptr;
}

bool CreateClient()
{
    NetHost = enet_host_create(NULL /* create a client host */,
        1 /* only allow 1 outgoing connection */,
        2 /* allow up 2 channels to be used, 0 and 1 */,
        0 /* assume any amount of incoming bandwidth */,
        0 /* assume any amount of outgoing bandwidth */);

    return NetHost != nullptr;
}

bool AttemptConnectToServer()
{
    ENetAddress address;
    /* Connect to 127.0.0.1 */
    enet_address_set_host(&address, "127.0.0.1");
    address.port = 1234;
    /* Initiate the connection, allocating the two channels 0 and 1. */
    Peer = enet_host_connect(NetHost, &address, 2, 0);
    return Peer != nullptr;
}

void HandleReceivePacket(const ENetEvent& event)
{
    if (IsServer)
    {
        string message;

        // If number is guessed correctly
        if (guessNumber == atoi((char*) event.packet->data)) 
        {
            ENetPacket* packet = enet_packet_create(successMessage.c_str(),
                strlen(successMessage.c_str()) + 1,
                ENET_PACKET_FLAG_RELIABLE);

            // Send win message
            enet_peer_send(event.peer, 0, packet);

            packet = enet_packet_create(otherPlayerWonMessage.c_str(),
                strlen(otherPlayerWonMessage.c_str()) + 1,
                ENET_PACKET_FLAG_RELIABLE);

            // Send lose message to other players
            for (int i = 0; i < 32; i++) {
                ENetPeer* peer = NetHost->peers + i;
                if (peer->connectID != event.peer->connectID)
                {
                    enet_peer_send(peer, 0, packet);
                }
            }
        }

        else 
        {
            ENetPacket* packet = enet_packet_create(failureMessage.c_str(),
                strlen(failureMessage.c_str()) + 1,
                ENET_PACKET_FLAG_RELIABLE);

            // Send failure message
            enet_peer_send(event.peer, 0, packet);
        }

        enet_host_flush(NetHost);
    }

    else 
    {
        // Print out win, failure, or lose message
        cout << event.packet->data << endl;

        // If win or lose, end game
        if ((char*)event.packet->data == successMessage || (char*)event.packet->data == otherPlayerWonMessage)
        {
            quit = true;
        }
    }

    /* Clean up the packet now that we're done using it. */
    enet_packet_destroy(event.packet);
    {
        enet_host_flush(NetHost);
    }
}

void ServerProcessPackets()
{
    while (!quit)
    {
        ENetEvent event;
        while (enet_host_service(NetHost, &event, 1000) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                cout << "A new client connected from "
                    << event.peer->address.host
                    << ":" << event.peer->address.port
                    << endl;
                /* Store any relevant client information here. */
                event.peer->data = (void*)("Client information");
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                HandleReceivePacket(event);
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                cout << (char*)event.peer->data << " disconnected." << endl;
                /* Reset the peer's client information. */
                event.peer->data = NULL;
                // Notify remaining player that the game is done due to player leaving
            }
        }
    }
}

void ClientProcessPackets()
{
    while (!quit)
    {
        ENetEvent event;
        /* Wait up to 1000 milliseconds for an event. */
        while (enet_host_service(NetHost, &event, 1000) > 0)
        {
            switch (event.type)
            {
            case  ENET_EVENT_TYPE_CONNECT:
                cout << "Connection succeeded " << endl;
                cout << "Guess a number between 0 and " << maximumGuessNumber << endl;
                cout << "Enter 'quit' to exit the game" << endl;
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                HandleReceivePacket(event);
                break;
            }
        }
    }
}

int main(int argc, char** argv)
{
    if (enet_initialize() != 0)
    {
        fprintf(stderr, "An error occurred while initializing ENet.\n");
        cout << "An error occurred while initializing ENet." << endl;
        return EXIT_FAILURE;
    }
    atexit(enet_deinitialize);

    cout << "1) Create Server " << endl;
    cout << "2) Create Client " << endl;
    int UserInput;
    cin >> UserInput;
    if (UserInput == 1)
    {
        if (!CreateServer())
        {
            fprintf(stderr,
                "An error occurred while trying to create an ENet server.\n");
            exit(EXIT_FAILURE);
        }

        IsServer = true;
        cout << "Waiting for players to join..." << endl;
        cout << "Enter 'quit' at any time to close server" << endl;
        PacketThread = new thread(ServerProcessPackets);
        QuitServerThread = new thread(QuitServer);
        srand((unsigned int)time(NULL));
        guessNumber = rand() % (maximumGuessNumber + 1);
    }
    else if (UserInput == 2)
    {
        if (!CreateClient())
        {
            fprintf(stderr,
                "An error occurred while trying to create an ENet client host.\n");
            exit(EXIT_FAILURE);
        }

        if (!AttemptConnectToServer())
        {
            fprintf(stderr,
                "No available peers for initiating an ENet connection.\n");
            exit(EXIT_FAILURE);
        }

        PacketThread = new thread(ClientProcessPackets);
        GuessThread = new thread(GuessNumber);
    }
    else
    {
        cout << "Invalid Input" << endl;
    }

    TerminateThreads();

    if (NetHost != nullptr)
    {
        enet_host_destroy(NetHost);
    }

    return EXIT_SUCCESS;
}
