# Awale-Game

## Compilation
To compile the client and server, you can run the following ``make`` commmand:
```
make all
```
## Usage
You have to firstly run the server, by specifying the listening port:
```
./server [PORT_NUMBER]
``` 
Once a server is listening to incoming connections, you can run the client and provide the server IP and port as arguments:
```
./client [SERVER_IP] [SERVER_PORT]
```
## Instructions and features
When running the client, the player has to register by providing a username:
```
/register [USERNAME]
``` 
Following the registration step, the player has access to the following features:
* /add-bio [BIO], to add a bio to this account.
* /list-bio [USERNAME], to display the bio of another player.
* /send-msg [USERNAME] [MSG], to send a message to another user identified by this username.
* /add-friend [USERNAME], to send a friend request to a player. **This requires the approval of the other player in order for him to be added to the caller's list of friends**.
* /invite [USERNAME], to invite another user for a game. **This requires the approval of the other player for the game to start.**
* /list-friends, to list the current player's friends.
* /list-online-users, to list the users that are in the state.ONLINE_IDLE, and therefore connected to the server, but not in game. 
* /list-current-games, to list the ongoing games between two players. 
* /quit, to disconnect from the server.
* /observer-mode [USERNAME], to allow the user to observer ongoing games.
* /stop-observe, to stop observing.
* /help, to display the above commands again.

Once inside a game, a player can run the following commands:
* 1-6, selecting a position between 1 and 6 in the game board.
* s, to save the game.
* l, to load a saved game.
* /send-msg [USERNAME] [MSG], to send a message to the other player.
* /private-mode, to switch the ongoing game into private-mode, not allowing observers that are not part of the caller's group of friends.
* /help-menu, to list the help-menu which displays the above commands again.  