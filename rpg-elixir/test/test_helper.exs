# Tests must never touch the real save directory, and never animate.
data_dir = Path.join(System.tmp_dir!(), "rpg-elixir-tests-#{System.unique_integer([:positive])}")
System.put_env("RPG_DATA_DIR", data_dir)
System.put_env("RPG_NO_ANIM", "1")
System.delete_env("RPG_SHARED_DIR")

ExUnit.start()
