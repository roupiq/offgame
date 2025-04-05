import random
import time
import subprocess
import argparse
from dataclasses import dataclass
from enum import Enum
from typing import Tuple, List

# Following class is a code from: https://stackoverflow.com/questions/12151306/argparse-way-to-include-default-values-in-help
class ExplicitDefaultsHelpFormatter(argparse.ArgumentDefaultsHelpFormatter):
    def _get_help_string(self, action):
        if action.default is None or action.default is False:
            return action.help
        return super()._get_help_string(action)

def getArgParser() -> argparse.ArgumentParser:
	arg_parser = argparse.ArgumentParser(
		prog='off_game',
		description='Game Runner.',
		formatter_class = ExplicitDefaultsHelpFormatter
	)
	arg_parser.add_argument('-r', '--red', type=str, help='Red player executable path', required=True)
	arg_parser.add_argument('-b', '--blue', type=str, help='Blue player executable path', required=True)
	arg_parser.add_argument('-s', '--silent', action='store_true', help='Don\'t print game state after each round.')
	arg_parser.add_argument('-t', '--timeout', type=int, default=500, help='Executables timeout in milliseconds.')
	arg_parser.add_argument('-n', '--height', type=int, default=20, help='Game field height.')
	arg_parser.add_argument('-m', '--width', type=int, default=30, help='Game field width.')
	arg_parser.add_argument('-w', '--wall-count', type=int, default=30, help='Approximated wall count.')
	arg_parser.add_argument('--round-count', type=int, default=100, help='Number of rounds after which there will be tie.')
	arg_parser.add_argument('--seed', type=int, help='Game seed (if not given, then seed will be based of system time).')
	arg_parser.add_argument('--wait', type=int, help='Wait time in millisecond between round.')
	arg_parser.add_argument('--nice-print', action='store_true', help='Print game state in nice way, where each tile is just one char (not four). It might hide some bullets.')
	arg_parser.add_argument('--id', type=int, help='Game id')

	return arg_parser

BULLET_UP    = "^"
BULLET_DOWN  = "v"
BULLET_RIGHT = ">"
BULLET_LEFT  = "<"

class TileType(Enum):
	STANDARD = 0
	WALL     = 1

class PlayersID(Enum):
	RED  = 0
	BLUE = 1

def showPlayerID(player_id: PlayersID) -> str:
	match player_id:
		case PlayersID.RED:
			return "R"
		case PlayersID.BLUE:
			return "B"

@dataclass
class Bullet:
	position: Tuple[int, int]
	direction: Tuple[int, int]

@dataclass
class Player:
	player_type: PlayersID
	position: Tuple[int, int]
	direction: Tuple[int, int]

	def show(self) -> str:
		return showPlayerID(self.player_type)

class Tile:
	def __init__(self):
		self.type = TileType.STANDARD

	def	showStr(self) -> str:
		if (self.type == TileType.STANDARD):
			return " "
		elif (self.type == TileType.WALL):
			return "#"
		else:
			raise Exception("Invalid TileType")

# @note: when adding new move profile, remember to update runExec function
class MoveProfile(Enum):
	MOVE_UP     = 0
	MOVE_DOWN   = 1
	MOVE_LEFT   = 2
	MOVE_RIGHT  = 3
	SHOOT_UP    = 4
	SHOOT_DOWN  = 5
	SHOOT_LEFT  = 6
	SHOOT_RIGHT = 7
	WAIT        = 8
	SURRENDER   = 9

@dataclass
class Move:
	profiles: List[MoveProfile]

class GameLogic:
	def __init__(self, n: int, m: int, wall_count: int, seed = None):
		self.n = n
		self.m = m

		self.tiles = [[Tile() for _ in range(m)] for _ in range(n)]
		self.bullets = []
		self.players = [Player(PlayersID.RED, (1, 1), (0, 1)), Player(PlayersID.BLUE, (n-2, m-2), (0, 1))]

		if (seed is not None):
			random.seed(seed)
		for _ in range(wall_count // 2):
			x = random.randint(0, n-1)
			y = random.randint(0, m-1)
			self.tiles[x][y].type = TileType.WALL
			self.tiles[n - x - 1][m - y - 1].type = TileType.WALL

		self.tiles[1][1].type = TileType.STANDARD
		self.tiles[n-2][m-2].type = TileType.STANDARD

		# fill border tiles with walls:
		for i in range(n):
			self.tiles[i][0].type   = TileType.WALL
			self.tiles[i][m-1].type = TileType.WALL
		for i in range(m):
			self.tiles[0][i].type   = TileType.WALL
			self.tiles[n-1][i].type = TileType.WALL

	def moveBulletsOneStep(self):
		for bullet in self.bullets:
			x, y = bullet.position
			dx, dy = bullet.direction
			new_x, new_y = x + dx, y + dy

			if (new_x < 0 or new_x >= self.n or new_y < 0 or new_y >= self.m):
				self.bullets.remove(bullet)
			else:
				if self.tiles[new_x][new_y].type == TileType.WALL:
					bullet.direction = (-dx, -dy)
				else:
					bullet.position = (new_x, new_y)

	def applyMove(self, move: Move) -> List[PlayersID]:
		"""Return list of hits"""

		assert len(move.profiles) == len(self.players)
		assert len(self.players) == 2

		output = []

		if move.profiles[0] == MoveProfile.SURRENDER:
			output.append(self.players[0].player_type)
		if move.profiles[1] == MoveProfile.SURRENDER:
			output.append(self.players[0].player_type)

		if len(output) > 0:
			return output

		for (player, profile) in zip(self.players, move.profiles):
			player.old_position = player.position
			match profile:
				case MoveProfile.MOVE_UP:
					player.position = (max(player.position[0] - 1, 0), player.position[1])
				case MoveProfile.MOVE_DOWN:
					player.position = (min(player.position[0] + 1, self.n - 1), player.position[1])
				case MoveProfile.MOVE_LEFT:
					player.position = (player.position[0], max(player.position[1] - 1, 0))
				case MoveProfile.MOVE_RIGHT:
					player.position = (player.position[0], min(player.position[1] + 1, self.m - 1))
				case MoveProfile.SHOOT_UP:
					self.bullets.append(Bullet(player.position, (-1, 0)))
				case MoveProfile.SHOOT_DOWN:
					self.bullets.append(Bullet(player.position, (1, 0)))
				case MoveProfile.SHOOT_LEFT:
					self.bullets.append(Bullet(player.position, (0, -1)))
				case MoveProfile.SHOOT_RIGHT:
					self.bullets.append(Bullet(player.position, (0, 1)))
				case MoveProfile.WAIT:
					pass
				case _:
					raise Exception("Invalid MoveProfile")
			if self.tiles[player.position[0]][player.position[1]].type == TileType.WALL:
				player.position = player.old_position

		if self.players[0].position == self.players[1].position:
			self.players[0].position = self.players[0].old_position
			self.players[1].position = self.players[1].old_position

		self.moveBulletsOneStep()

		for player in self.players:
			for bullet in self.bullets:
				if player.position == bullet.position:
					# hit
					output.append(player.player_type)
		
		return output

	def showStr(self, nice: bool) -> str:
		tiles_str = [[[" ", " ", " ", " "] for i in range(self.m)] for _ in range(self.n)]

		for i in range(len(self.tiles)):
			for j in range(len(self.tiles[i])):
				tiles_str[i][j][0] = self.tiles[i][j].showStr()

		for player in self.players:
			x, y = player.position
			tiles_str[x][y][0] = player.show()

		for bullet in self.bullets:
			x, y = bullet.position
			match bullet.direction:
				case (-1, 0):
					tiles_str[x][y][0] = BULLET_UP
				case (1, 0):
					tiles_str[x][y][1] = BULLET_DOWN
				case (0, -1):
					tiles_str[x][y][2] = BULLET_LEFT
				case (0, 1):
					tiles_str[x][y][3] = BULLET_RIGHT

		if not nice:
			return "".join([item for row in tiles_str for tile in (row+["\n"]) for item in tile]) 
		else:
			out = []
			for row in tiles_str:
				for tile in row:
					to_add = " "
					for c in tile[::-1]:
						if c != " ":
							to_add = c
							break
					out.append(to_add)
				out.append("\n")
			return ''.join(out)

	def showForUser(self, nice: bool) -> str:
		output = []
		output.extend([str(self.n), " ", str(self.m)])
		output.append("\n")
		output.append(self.showStr(nice = nice))
		return ''.join(output)

class Game:
	def __init__(self, n: int, m: int, wall_count: int, red_player_exec: str, blue_player_exec: str, seed = None, max_rounds = 100, timeout = 5000):
		self.game_state   = GameLogic(n, m, wall_count, seed)
		self.round_number = 0
		self.max_rounds   = max_rounds
		self.timeout      = timeout

		self.red_player_exec  = red_player_exec
		self.blue_player_exec = blue_player_exec

	def showForUser(self, who: PlayersID | None = None, nice: bool = False) -> str:
		output = [self.game_state.showForUser(nice = nice)]
		output.append(str(self.round_number))
		output.append("\n")
		if (who is not None):
			output.append(showPlayerID(who))
			output.append("\n")
		return ''.join(output)
	
	def runExec(self, exec_str: str, who: PlayersID) -> MoveProfile:
		try:
			out = subprocess.check_output(exec_str, input = self.showForUser(who), text=True, timeout = self.timeout/1000)
			out_int = int(out)
			if out_int < 0 or out_int > 9:
				return MoveProfile.SURRENDER
			return MoveProfile(out_int)
		except subprocess.TimeoutExpired:
			print("Warning: Exec hit timeout")
			return MoveProfile.WAIT
		except ValueError:
			print("Surrender")
			return MoveProfile.SURRENDER
	
	def performMoveWithExec(self):
		"""Return list of hits or "tie" string if round limit was hit."""

		red_move  = self.runExec(self.red_player_exec, who = PlayersID.RED)
		blue_move = self.runExec(self.blue_player_exec, who = PlayersID.BLUE)

		out = self.game_state.applyMove(Move([red_move, blue_move]))
		
		self.round_number += 1

		if self.round_number > self.max_rounds:
			return "tie"
		
		return out
		
	# def playGame(self):

def print_colored_board(input_board: str):
	print(input_board.replace("R", "\033[91mR\033[0m").replace("B", "\033[94mB\033[0m"))

def save_res(res, id):
	with open(f"results{id}", "w") as file:
		file.write(res)


def main():
	
	arg_parser = getArgParser()
	args = arg_parser.parse_args()

	game = Game(
		args.height,
		args.width,
		args.wall_count,
		args.red,
		args.blue,
		args.seed,
		args.round_count,
		args.timeout,
	)

	if not args.silent:
		print_colored_board(game.showForUser(nice = args.nice_print))
		print()

	for _ in range(args.round_count):
		out = game.performMoveWithExec()

		if not args.silent:
			print_colored_board(game.showForUser(nice = args.nice_print))
			print()

		out = list(dict.fromkeys(out))

		if out == "tie":
			print("Tie!")
			save_res("TIE", args.id)
			return

		if len(out) == 2:
			print("Tie!")
			save_res("TIE", args.id)
			return

		if out != []:
			if len(out) != 1:
				print(out)
			assert len(out) == 1 
			if out[0] == PlayersID.BLUE:
				save_res("RED", args.id)
				print("Red player won!")
			elif out[0] == PlayersID.RED:
				save_res("BLUE", args.id)
				print("Blue player won!")
			return
		
		if args.wait is not None:
			time.sleep(args.wait / 1000)

	save_res("TIE", args.id)
	print("Tie!")
	return

if __name__ == "__main__":
	main()


