import subprocess, os
import re

main_loc = "/home/abhishekvjoshi/CSCE614/Project/CacheReusePred/"
logs_loc = "/home/abhishekvjoshi/CSCE614/Project/CacheReusePred/Logs/"

bmarks = ["crafty", "gap", "mcf", "parser", "gcc", "gzip", "vortex", "vpr", "bzip2", "twolf", "ammp", "applu", "apsi", "art", "equake", "fma3d", "galgel", "lucas", "mesa", "mgrid", "swim"]
regex = ["sim_IPC +[0-9]+.[0-9]+", "sim_total_insn +[0-9]+", "dl1.hits +[0-9]+", "dl1.misses +[0-9]+", "ul2.hits +[0-9]+", "ul2.misses +[0-9]+"]

os.chdir(main_loc)
# subprocess.call("mkdir Logs", shell=True)
# os.chdir(logs_loc)

# for bm in bmarks:
# 	subprocess.call("mkdir "+bm, shell=True)

tr_thres = 250
b_thres = -20
r_thres = 100
i_skip = 100
dl1_mpki_data = {}
dl2_mpki_data = {}
dl1_misses_data = {}
dl2_misses_data = {}
dl1_hits_data = {}
dl2_hits_data = {}
ins_data = {}
ipc_data = {}

for i, bm in enumerate(bmarks):
	if i != i_skip:
		subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
		os.chdir("spec2000args/"+bm)
		subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+ \
			logs_loc+bm+"/CacheReuse_t_"+str(tr_thres)+"_b_"+str(b_thres)+"_r_"+str(r_thres)+"_"+bm+"_logs.txt -cache:dl1 dl1:1024:32:8:l -cache:dl2 ul2:4096:64:8:l", shell=True)	
		os.chdir(main_loc)

for j, bm in enumerate(bmarks):
	# for i, pat in enumerate(regex):	
		if j != i_skip:
			f = open(logs_loc+bm+"/CacheReuse_t_"+str(tr_thres)+"_b_"+str(b_thres)+"_r_"+str(r_thres)+"_"+bm+"_logs.txt")
			txt = f.read()
			ipc = re.findall(regex[0], txt)
			print j, ipc
			ipc_data[j] = float(ipc[0].split()[1])
			ins = re.findall(regex[1], txt)
			ins_data[j] = float(ins[0].split()[1])
			dl1_hits = re.findall(regex[2], txt)
			dl1_hits_data[j] = float(dl1_hits[0].split()[1])
			dl2_hits = re.findall(regex[4], txt)
			dl2_hits_data[j] = float(dl2_hits[0].split()[1])
			dl1_misses = re.findall(regex[3], txt)
			dl1_misses_data[j] = float(dl1_misses[0].split()[1])
			dl2_misses = re.findall(regex[5], txt)
			dl2_misses_data[j] = float(dl2_misses[0].split()[1])
			dl1_mpki_data[j] = 1000*float(dl1_misses_data[j])/(float(ins_data[j]))
			dl2_mpki_data[j] = 1000*float(dl2_misses_data[j])/(float(ins_data[j]))
			f.close()

# print "Left out benchmark: %s" % (bmarks[i_skip])
print "Training threshold is: %d" % (tr_thres)
print "Bypass threshold is: %d" % (b_thres)
print "Replacement threshold is: %d" % (r_thres)
print "Average IPC is: %f" % (float(sum(ipc_data))/float(len(ipc_data)))
print "Average number of hits in Cache level 1 is: %f" % (float(sum(dl1_hits_data.values()))/float(len(dl1_hits_data.values())))
print "Average number of hits in Cache level 2 is: %f" % (float(sum(dl2_hits_data.values()))/float(len(dl2_hits_data.values())))
print "Average number of misses in Cache level 1 is: %f" % (float(sum(dl1_misses_data.values()))/float(len(dl1_misses_data.values())))
print "Average number of miesses in Cache level 2 is: %f" % (float(sum(dl2_misses_data.values()))/float(len(dl2_misses_data.values())))
print "Average MPKI for Cache level 1 is: %f" % (float(sum(dl1_mpki_data.values()))/float(len(dl1_mpki_data.values())))
print "Average MPKI for Cache level 2 is: %f" % (float(sum(dl2_mpki_data.values()))/float(len(dl2_mpki_data.values())))

# for bm in bmarks:
# 	subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
# 	os.chdir("spec2000args/"+bm)
# 	subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"cache_fifo_"+bm+"_logs.txt -cache:dl1 dl1:128:32:4:f -cache:dl2 ul2:1024:64:4:f", shell=True)	
# 	os.chdir(main_loc)

# for bm in bmarks:
# 	subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
# 	os.chdir("spec2000args/"+bm)
# 	subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"cache_random_"+bm+"_logs.txt -cache:dl1 dl1:128:32:4:r -cache:dl2 ul2:1024:64:4:r", shell=True)	
# 	os.chdir(main_loc)

# for bm in bmarks:
# 	subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
# 	os.chdir("spec2000args/"+bm)
# 	subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"cache_plru_"+bm+"_logs.txt -cache:dl1 dl1:128:32:4:p -cache:dl2 ul2:1024:64:4:p", shell=True)	
# 	os.chdir(main_loc)


# for bm in bmarks:
# 	subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
# 	os.chdir("spec2000args/"+bm)
# 	subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"unmod_"+bm+"_logs.txt -bpred 2lev -bpred:2lev 1 512 8 0 -bpred:ras 8 -bpred:btb 64 2", shell=True)	
# 	os.chdir(main_loc)
	
# for bm in bmarks:
# 	subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
# 	os.chdir("spec2000args/"+bm)
# 	subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"unmod_GAg_"+bm+"_logs.txt -bpred 2lev -bpred:2lev 1 512 9 0 -bpred:ras 8 -bpred:btb 64 2", shell=True)	
# 	os.chdir(main_loc)
	
# for bm in bmarks:
# 	subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
# 	os.chdir("spec2000args/"+bm)
# 	subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"unmod_GAp_"+bm+"_logs.txt -bpred 2lev -bpred:2lev 1 512 6 0 -bpred:ras 8 -bpred:btb 64 2", shell=True)	
# 	os.chdir(main_loc)
	
# for bm in bmarks:
# 	subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
# 	os.chdir("spec2000args/"+bm)
# 	subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"unmod_PAg_"+bm+"_logs.txt -bpred 2lev -bpred:2lev 8 512 9 0 -bpred:ras 8 -bpred:btb 64 2", shell=True)	
# 	os.chdir(main_loc)
	
# for bm in bmarks:
# 	subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
# 	os.chdir("spec2000args/"+bm)
# 	subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"unmod_PAp_"+bm+"_logs.txt -bpred 2lev -bpred:2lev 8 512 6 0 -bpred:ras 8 -bpred:btb 64 2", shell=True)	
# 	os.chdir(main_loc)
	
# for bm in bmarks:
	# subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
	# os.chdir("spec2000args/"+bm)
	# subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"unmod_GAg_"+bm+"_logs.txt -bpred 2lev -bpred:2lev 1 512 9 0 -bpred:ras 8 -bpred:btb 64 2", shell=True)	
	# os.chdir(main_loc)

# for bm in bmarks:
	# subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
	# os.chdir("spec2000args/"+bm)
	# subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"unmod_GAg_"+bm+"_logs.txt -bpred 2lev -bpred:2lev 1 512 9 0 -bpred:ras 8 -bpred:btb 64 2", shell=True)	
	# os.chdir(main_loc)
	
# for bm in bmarks:
	# subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
	# os.chdir("spec2000args/"+bm)
	# subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"unmod_GAp_"+bm+"_logs.txt -bpred 2lev -bpred:2lev 1 512 6 0 -bpred:ras 8 -bpred:btb 64 2", shell=True)	
	# os.chdir(main_loc)
	
# for bm in bmarks:
	# subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
	# os.chdir("spec2000args/"+bm)
	# subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"unmod_PAg_"+bm+"_logs.txt -bpred 2lev -bpred:2lev 8 512 9 0 -bpred:ras 8 -bpred:btb 64 2", shell=True)	
	# os.chdir(main_loc)
	
# for bm in bmarks:
	# subprocess.call("cp runscripts/RUN"+bm+" spec2000args/"+bm, shell=True)
	# os.chdir("spec2000args/"+bm)
	# subprocess.call("./RUN"+bm+" ../../simplesim-3.0/sim-outorder ../../spec2000binaries/"+bm+"00.peak.ev6 -max:inst 50000000 -fastfwd 20000000 -redir:sim "+"unmod_PAp_"+bm+"_logs.txt -bpred 2lev -bpred:2lev 8 512 6 0 -bpred:ras 8 -bpred:btb 64 2", shell=True)	
	# os.chdir(main_loc)
	
# Create directories
# for bm in bmarks:
# 	os.chdir(logs_loc)
# 	subprocess.call("mkdir "+bm, shell=True)
	
# for bm in bmarks:
# 	os.chdir(main_loc+"spec2000args/"+bm)
	
# 	subprocess.call("cp cache_lru_"+bm+"_logs.txt "+logs_loc+bm+"/", shell=True)
# 	subprocess.call("cp cache_plru_"+bm+"_logs.txt "+logs_loc+bm+"/", shell=True)
# 	subprocess.call("cp cache_fifo_"+bm+"_logs.txt "+logs_loc+bm+"/", shell=True)
# 	subprocess.call("cp cache_random_"+bm+"_logs.txt "+logs_loc+bm+"/", shell=True)

	
# 	os.chdir(main_loc)
	