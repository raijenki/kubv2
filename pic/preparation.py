import os, json

# Path to the persistent volume
pvol_path = "/data/gem/"
input_location = "/home/inputfiles/"

# Get the inputfiles in which we should run our different pic instances
with open('/home/inputs.txt', 'r') as file:
    inputs = file.read().splitlines()

for index, gem in enumerate(inputs):
    gem_dir_path = os.path.join(pvol_path, str(index))
    os.mkdir(gem_dir_path)
    
    gem_input_file = input_location + gem
    with open(gem_input_file, "r") as file:
        gem_data = file.read()
    
    new_savedir = "SaveDirName = " + pvol_path + str(index) + "/"
    new_restartdir = "RestartDirName = " + pvol_path + str(index) + "/"
    update_contents = gem_data.replace("SaveDirName = data", new_savedir).replace("RestartDirName = data", new_restartdir)
    gem_input_file = pvol_path + str(index) + "/" + str(index) + ".inp"

    with open(gem_input_file, "w") as file:
        file.write(update_contents)
