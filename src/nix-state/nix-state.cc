#include <iostream>
#include <algorithm>
#include <sys/time.h>

#include "globals.hh"
#include "misc.hh"
#include "archive.hh"
#include "shared.hh"
#include "db.hh"
#include "util.hh"
#include "help.txt.hh"
#include "local-store.hh"
#include "derivations.hh"
#include "references.hh"
#include "store-state.hh"
#include "config.h"
#include "snapshot.hh"

using namespace nix;
using std::cin;
using std::cout;


typedef void (* Operation) (Strings opFlags, Strings opArgs);

//two global variables
string stateIdentifier;
string username;
string comment;
int revision_arg;
bool scanforReferences = false;
bool only_commit = false;
bool revert_recursively = false;


/************************* Build time Functions ******************************/



/************************* Build time Functions ******************************/



/************************* Run time Functions ******************************/

void printHelp()
{
    printMsg(lvlInfo, format("%1%") % helpText);
    //cout << string((char *) helpText, sizeof helpText);
}

Derivation getDerivation(const string & fullPath, const Strings & program_args, string state_identifier, Path & componentPath, Path & statePath, 
						 string & binary, string & derivationPath, bool isStateComponent,
						 bool getDerivers, PathSet & derivers) 	//optional
{
	//Parse the full path like /nix/store/...../bin/hello
    componentPath = fullPath.substr(nixStore.size() + 1, fullPath.size());		//+1 to strip off the /
    int pos = componentPath.find("/",0);
    componentPath = fullPath.substr(0, pos + nixStore.size() + 1);
    binary = fullPath.substr(pos + nixStore.size() + 1, fullPath.size());

	if( !(store->isValidPath(componentPath) || store->isValidStatePath(componentPath)) )
		throw UsageError(format("Path '%1%' is not a valid state or store path") % componentPath);

    //Check if path is store-statepath
    isStateComponent = store->isStateComponent(componentPath);
      
	//printMsg(lvlError, format("'%1%' - '%2%' - '%3%' - '%4%' - '%5%'") % componentPath % state_identifier % binary % username % program_args);
    
    if(isStateComponent)
    	derivers = store->queryDerivers(componentPath, state_identifier, username);
    else
    	derivers.insert(store->queryDeriver(componentPath));
    
    if(getDerivers == true)
    	return Derivation();
    
    if(isStateComponent){	
	    if(derivers.size() == 0)
	    	throw UsageError(format("There are no derivers with this combination of identifier '%1%' and username '%2%'") % state_identifier % username);
	    if(derivers.size() != 1)
	    	throw UsageError(format("There is more than one deriver with state_identifier '%1%' and username '%2%'") % state_identifier % username);
    }
    
    //Retrieve the derivation, there is only 1 drvPath in derivers
    derivationPath = *(derivers.begin());
    Derivation drv = derivationFromPath(derivationPath);
	
	if(isStateComponent){
    	DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    	statePath = stateOutputs.find("state")->second.statepath;
	}
	
	return drv;
}

//Wrapper
Derivation getDerivation_andCheckArgs_(Strings opFlags, Strings opArgs, Path & componentPath, Path & statePath, 
									   string & binary, string & derivationPath, bool isStateComponent, Strings & program_args,
									   bool getDerivers, PathSet & derivers) 	//optional
{
	if (!opFlags.empty()) throw UsageError("unknown flag");
    if ( opArgs.size() < 1) 
    	throw UsageError("you must specify at least the component path (optional are the program arguments wrapped like this \"$@\")");
    	
   	string fullPath = opArgs.front(); 
   	
    if(opArgs.size() > 1){
		
		opArgs.pop_front();
		for (Strings::iterator i = opArgs.begin(); i != opArgs.end(); ++i){
			string arg = *i;
			//printMsg(lvlError, format("Args: %1%") % arg);
			program_args.push_back(arg);			
		}
    }
   
    return getDerivation(fullPath, program_args, stateIdentifier, componentPath, statePath, binary, derivationPath, isStateComponent, getDerivers, derivers);	
}

//Wrapper
Derivation getDerivation_andCheckArgs(Strings opFlags, Strings opArgs, Path & componentPath, Path & statePath, 
									  string & binary, string & derivationPath, bool & isStateComponent, Strings & program_args)
{
	PathSet empty;
	return getDerivation_andCheckArgs_(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args, false, empty);
}

//
static void opShowDerivations(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string binary;
    PathSet derivers;
    string derivationPath;
    bool isStateComponent;
    Strings program_args;
    Derivation drv = getDerivation_andCheckArgs_(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args, true, derivers);
	
	if(!isStateComponent)
		throw UsageError(format("This path '%1%' is not a state-component path") % componentPath);
	
	for (PathSet::iterator i = derivers.begin(); i != derivers.end(); ++i)
     	printMsg(lvlError, format("%1%") % (*i));
}


//Prints the statepath of a component - indetiefier combination
static void opShowStatePath(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string binary;
    string derivationPath;
    bool isStateComponent;
    Strings program_args;
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args);
    
    if(!isStateComponent)
		throw UsageError(format("This path '%1%' is not a state-component path") % componentPath);
    
	printMsg(lvlError, format("%1%") % statePath);
}


static void revertToRevision(Strings opFlags, Strings opArgs)
{
	Path componentPath;
    Path statePath;
    string binary;
    string derivationPath;
    bool isStateComponent;
    Strings program_args;
    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args);
    
    bool recursive = revert_recursively;
    
    store->revertToRevision(componentPath, derivationPath, statePath, revision_arg, recursive);
}


static void queryAvailableStateRevisions(Strings opFlags, Strings opArgs)
{
	Path statePath;

	if(store->isValidStatePath(*(opArgs.begin())))
		statePath = *(opArgs.begin());
	else{
		Path componentPath;
	    string binary;
	    string derivationPath;
	    bool isStateComponent;
	    Strings program_args;
	    Derivation drv = getDerivation_andCheckArgs(opFlags, opArgs, componentPath, statePath, binary, derivationPath, isStateComponent, program_args);
	}
	
	//Unshare if neccacary
	PathSet nonSharedPaths;
	nonSharedPaths.insert(statePath);
	Path nonSharedStatePath = *((store->toNonSharedPathSet(nonSharedPaths)).begin());			//TODO CHECK IF THIS WORKS !!!!!!!!
	if(nonSharedStatePath != statePath){
		printMsg(lvlError, format("The statePath is shared with this path %1%") % nonSharedStatePath);
		statePath = nonSharedStatePath;
	}
	
	RevisionInfos revisions;
	bool notEmpty = store->queryAvailableStateRevisions(statePath, revisions);
		
	if(!notEmpty){
		printMsg(lvlError, format("No revisions yet for: %1%") % statePath);
		return;	
	}
	
	//Sort ourselfes to create a nice output
	IntVector revisions_sort;
	int highestrev;
	for (RevisionInfos::iterator i = revisions.begin(); i != revisions.end(); ++i){
		int rev = (*i).first;
		revisions_sort.push_back(rev);
		if(rev > highestrev)
			highestrev = rev;
	}
	sort(revisions_sort.begin(), revisions_sort.end());

	int max_size = int2String(highestrev).length();
	for (IntVector::iterator i = revisions_sort.begin(); i != revisions_sort.end(); ++i)
	{
		int rev = *i;
		string rev_s = padd(int2String(rev), '0' , max_size, true);	//pad revisions with a 0
		unsigned int ts = revisions[rev].timestamp;
		time_t time = atoi(unsignedInt2String(ts).c_str());
		string human_date = ctime(&time);
		human_date.erase(human_date.find("\n",0),1);	//remove newline
		string comment = revisions[rev].comment;
		
		if(trim(comment) != "")
			printMsg(lvlError, format("Rev. %1% @ %2% (%3%) -- %4%") % rev_s % human_date % ts % comment);
		else
			printMsg(lvlError, format("Rev. %1% @ %2% (%3%)") % rev_s % human_date % ts);
	}	
}



static void opRunComponent(Strings opFlags, Strings opArgs)
{
    //get the all the info of the component that is being called (we dont really use it yet)
    Path root_componentPath;
    Path root_statePath;
    string root_binary;
	string root_derivationPath;
	bool root_isStateComponent;
	Strings root_program_args;
    Derivation root_drv = getDerivation_andCheckArgs(opFlags, opArgs, root_componentPath, root_statePath, root_binary, root_derivationPath, root_isStateComponent, root_program_args);
    
    //printMsg(lvlError, format("compp: '%1%'\nstatep: '%2%'\nbinary: '%3%'\ndrv:'%4%'") % root_componentPath % root_statePath % root_binary % root_derivationPath);
    
    //TODO
    //Check for locks ... ? or put locks on the neseccary state components
    //WARNING: we need to watch out for deadlocks!
	//add locks ... ?
	//svn lock ... ?
	
	
		
	//******************* Run ****************************
	
	if(!only_commit){
    	if( ! FileExist(root_componentPath + root_binary) )
    		throw Error(format("You must specify the full binary path: '%1%'") % (root_componentPath + root_binary));
    	
    	string root_args = "";
    	for (Strings::iterator i = root_program_args.begin(); i != root_program_args.end(); ++i){
    		if(*i == "--help" || *i == "--version")
    			printMsg(lvlError, format("%1%") % padd("", '-', 100));
    		//printMsg(lvlError, format("ARG %1%") % *i);
    		root_args += " \"" + *i + "\"";
    		
    		//TODO also scan the parameters for state or component hashes?
    		//program_args
    	}
    	
    	printMsg(lvlError, format("Command: '%1%'")	% (root_componentPath + root_binary + root_args));
		executeShellCommand(root_componentPath + root_binary + root_args);
	}
  	
  	//////////////////////////////
  	
  	//TODO
	Transaction txn;
   	//createStoreTransaction(txn);
  		
	//******************* Scan for new references if neccecary
   	if(scanforReferences)
  		store->scanAndUpdateAllReferences(root_statePath, true);		//TODO make recursive a paramter?

	//get all current (maybe updated by the scan) dependecies (if neccecary | recusively) of all state components that need to be updated
    PathSet statePaths;
	store->storePathRequisites(root_componentPath, false, statePaths, false, true, -1);
	statePaths.insert(root_statePath);

	//Start transaction TODO
    
    //Replace all shared paths in the set for their real paths 
    statePaths = store->toNonSharedPathSet(statePaths);
	
	//******************* With everything in place, we call the commit script on all statePaths (in)directly referenced **********************
	
	//Commit all statePaths
	RevisionClosure rivisionMapping;
	for (PathSet::iterator i = statePaths.begin(); i != statePaths.end(); ++i)		//TODO first commit own state path?
		rivisionMapping[*i] = store->commitStatePath(*i);
	
	//Save new revisions
	store->setStateRevisions(rivisionMapping, root_statePath, comment);				//TODO how about the txn?
	
	//Commit transaction
	//txn.commit();
	
	//Debugging
	RevisionClosure getRivisions;
	RevisionClosureTS empty;
	bool b = store->queryStateRevisions(root_statePath, getRivisions, empty, -1);
	for (RevisionClosure::iterator i = getRivisions.begin(); i != getRivisions.end(); ++i){
		//printMsg(lvlError, format("State %1% has revision %2%") % (*i).first % int2String((*i).second));
	}
	
	
}


void run(Strings args)
{
    Strings opFlags, opArgs;
    Operation op = 0;
    

	/* test *
	store = openStore();
	Path p = "/nix/store/l569q3a2cfx834mcf3vhwczjgbaljnp7-hellohardcodedstateworld-1.0";										//
	store->addUpdatedStateDerivation("/nix/store/63xcbrk3v5nbn9qla7rwnx6rvz3iqm5l-hellohardcodedstateworld-1.0.drv", p);		//
	Path p2 = "/nix/store/4ycq45hsgc8yaj4vwafx3lgd473jaqwg-hellohardcodedstateworld-1.0";
	store->addUpdatedStateDerivation("/nix/store/s6wggk924jx0gcb0l29ra4g9fxa3b4pp-hellohardcodedstateworld-1.0.drv", p2);		//
	store->updateAllStateDerivations();
	return;
	
	string a = makeStatePathFromGolbalHash("8f3b56a9a985fce54fd88c3e95a81a4b6b11fb98da12b977aee7f278c73ad3d7-hellohardcodedstateworld-1.0-test2", "kaaz");
	printMsg(lvlTalkative, format("%1%") % a);
	return;
	
	printMsg(lvlTalkative, format("Result: \"%1%\"") % getCallingUserName());
	return;
	
	store = openStore();
	store->addStateDeriver("/nix/store/0a151npn1aps8w75kpz2zm1yl3v11kbr-hellostateworld-1.0.drv", "/nix/store/k4v52ql98x2m09sb5pz7w1lrd4hamsm0-hellostateworld-1.0");
	store->addStateDeriver("/nix/store/2hpx60ibdfv2pslg4rjvp177frijamvi-hellostateworld-1.0.drv", "/nix/store/k4v52ql98x2m09sb5pz7w1lrd4hamsm0-hellostateworld-1.0");
	return;
		
	store = openStore();
	printMsg(lvlError, format("1: %1%") % bool2string( store->isStateComponent("/nix/store/7xkw5fkz5yw7dpx0pc6l12bh9a56135c-hellostateworld-1.0") ) );
	printMsg(lvlError, format("2: %1%") % bool2string( store->isStateComponent("/nix/store/05441jm8xmsidqm43ivk0micckf0mr2m-nvidiaDrivers") ) );
	printMsg(lvlError, format("3: %1%") % bool2string( isState Drv Path("/nix/store/2hpx60ibdfv2pslg4rjvp177frijamvi-hellostateworld-1.0.drv") ) );
	
	store = openStore();
	Path p = store->queryStatePathDrv("/nix/state/6g6kfgimz8szznlshf13s29fn01zp99d-hellohardcodedstateworld-1.0-test2");
	printMsg(lvlError, format("Result: %1%") % p);
	return;

	string path = "afddsafsdafsdaf.drv";
	printMsg(lvlError, format("Result: %1%") % path.substr(path.length() - 4,path.length()));

	printMsg(lvlError, format("AA: %1%") % isStorePath("/nix/store/hbxqq4d67j2y21xzp7yp01qjfkcjjbc7-hellohardcodedstateworld-1.0"));
	printMsg(lvlError, format("AA: %1%") % isStorePath("/nix/state/0qhlpz1ji4gvg3j6nk5vkcddmi3m5x1r-hellohardcodedstateworld-1.0-test2"));
	printMsg(lvlError, format("AA: %1%") % isStatePath("/nix/store/hbxqq4d67j2y21xzp7yp01qjfkcjjbc7-hellohardcodedstateworld-1.0"));
	printMsg(lvlError, format("AA: %1%") % isStatePath("/nix/state/0qhlpz1ji4gvg3j6nk5vkcddmi3m5x1r-hellohardcodedstateworld-1.0-test2"));

	PathSet p1;
	PathSet p2;
	PathSet p3;
	PathSet p4;
	p1.insert("a");
	p1.insert("c");		//old
	p1.insert("b");
	p2.insert("b");
	p2.insert("a");
	p2.insert("cc");	//new
	p2.insert("x");		//new
	pathSets_difference(p1,p2,p3,p4);
	pathSets_union(p1,p2);

	store->scanForAllReferences("/nix/state/i06flm2ahq5s0x3633z30dnav9f1wkb5-hellohardcodedstateworld-dep1-1.0-test");

	store = openStore();
	//setReferences_statePath("/nix/state/afsdsdafsdaf-sdaf", 7);
	
	Paths p1;
	p1.push_back("a");
	p1.push_back("b");
	p1.push_back("c");
	Paths p2;
	p2.push_back("b");
	p2.push_back("d");
	
	PathSet px = pathSets_union(PathSet(p1.begin(), p1.end()), PathSet(p2.begin(), p2.end()));
	
	for (PathSet::iterator i = px.begin(); i != px.end(); ++i)
		printMsg(lvlError, format("MERGED: %1%") % *i);
			
	Database nixDB;
	Path statePath = "afsdsdafsadf-sda-fsda-f-sdaf-sdaf";
	int revision = 5;
	Path statePath2;
	Path gets = nixDB.makeStatePathRevision(statePath, revision);
    int revision2;
    nixDB.splitStatePathRevision(gets, statePath2, revision2);
	printMsg(lvlError, format("'%1%' '%2%'") % statePath2 % int2String(revision2));
	
	store = openStore();
	Derivation drv = derivationFromPath("/nix/store/r2lvhrd8zhb877n07cqvcyp11j9ws5p0-hellohardcodedstateworld-dep1-1.0.drv");
	readRevisionNumbers(drv);
	
	
	Strings strings;
	strings.push_back("1");
	strings.push_back("3");
	strings.push_back("2");
	string packed = packStrings(strings);
	printMsg(lvlError, format("PA '%1%'") % packed);
	Strings strings2 = unpackStrings(packed);
	for (Strings::iterator i = strings2.begin(); i != strings2.end(); ++i)
		printMsg(lvlError, format("UN '%1%'") % *i);

	//updateRevisionNumbers("/nix/state/xf582zrz6xl677llr07rvskgsi3dli1d-hellohardcodedstateworld-dep1-1.0-test");
	//return;
	
	//auto sort
	map<string, string> test;
	test["q"] = "324";
	test["c"] = "3241";
	test["a"] = "a";
	for (map<string, string>::const_iterator j = test.begin(); j != test.end(); ++j)
		printMsg(lvlError, format("KEY: '%1%'") % (*j).first);
	printMsg(lvlError, format("NOW: '%1%'") % getTimeStamp());
	return;

	map<string, int> test;
	test["a"] = 1;
	test["b"] = 2;
	printMsg(lvlError, format("NOW: '%1%'") % test["q"]);
	return;	
	
	printMsg(lvlError, format("NOW: '%1%'") % pathExists("/etc") );
	printMsg(lvlError, format("NOW: '%1%'") % pathExists("/etc/passwd") );
	return;
	

	printMsg(lvlError, format("NOW: '%1%'") % FileExist("/nix/store/65c7p6c8j0vy6b8fjgq84zziiavswqha-hellohardcodedstateworld-1.0/") );
	printMsg(lvlError, format("NOW: '%1%'") % FileExist("/nix/store/65c7p6c8j0vy6b8fjgq84zziiavswqha-hellohardcodedstateworld-1.0/bin/hello") );
	printMsg(lvlError, format("NOW: '%1%'") % IsDirectory("/nix/store/65c7p6c8j0vy6b8fjgq84zziiavswqha-hellohardcodedstateworld-1.0/") );
	printMsg(lvlError, format("NOW: '%1%'") % IsDirectory("/nix/store/65c7p6c8j0vy6b8fjgq84zziiavswqha-hellohardcodedstateworld-1.0/bin/hello") );
	printMsg(lvlError, format("NOW: '%1%'") % FileExist("/nix/store/65c7p6c8j0vy6b8fjgq8") );
	printMsg(lvlError, format("NOW: '%1%'") % IsDirectory("/nix/store/65c7p6c8j0vy6b8fjg") );

	store = openStore();
	
	// /nix/state/g8vby0bjfrs85qpf1jfajrcrmlawn442-hellohardcodedstateworld-1.0-
	// /nix/state/6l93ff3bn1mk61jbdd34diafmb4aq7c6-hellohardcodedstateworld-1.0-
	// /nix/state/x8k4xiv8m4zmx26gmb0pyymmd6671fyy-hellohardcodedstateworld-1.0-
	
	PathSet p = store->getSharedWithPathSetRec("/nix/state/6l93ff3bn1mk61jbdd34diafmb4aq7c6-hellohardcodedstateworld-1.0-");
	for (PathSet::iterator j = p.begin(); j != p.end(); ++j)
		printMsg(lvlError, format("P: '%1%'") % *j );
	return;

	printMsg(lvlError, format("header: '%1%'") % nixExt3CowHeader);
	
	printMsg(lvlError, format("Username fail: '%1%'") % uidToUsername(23423));		//Segfaults correctly

	return;

	*/		
	
	/* test */
	
    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;

        Operation oldOp = op;
		
        if (arg == "--run" || arg == "-r")
            op = opRunComponent;
        else if (arg == "--commit-only"){
			op = opRunComponent;
			only_commit = true;
    	}
		else if (arg == "--showstatepath")
			op = opShowStatePath;
		else if (arg == "--showderivations")
			op = opShowDerivations;
		else if (arg == "--showrevisions")
			op = queryAvailableStateRevisions;
		else if (arg.substr(0,21) == "--revert-to-revision="){
			op = revertToRevision;
			bool succeed = string2Int(arg.substr(21,arg.length()), revision_arg);
			if(!succeed)
				throw UsageError("The given revision is not a valid number");
		}

        /*
		--run-without-commit

		--show-revision-path=....
		
		--showrevisions
		
		--revert-to-revision=
		
		--share-from
		
		--unshare
				
		OPTIONAL
		
		--scanreferences
								
		/////////////////////
		
		--backup ?
		
		--exclude-commit-paths
		
		TODO update getDerivation in nix-store to handle state indentifiers
		
		--delete-revision
		
        */
        
        else if (arg == "--scanreferences")
        	scanforReferences = true;        
        else if (arg.substr(0,13) == "--identifier=")
        	stateIdentifier = arg.substr(13,arg.length());
        else if (arg.substr(0,7) == "--user=")
        	username = arg.substr(7,arg.length());
        else if (arg.substr(0,10) == "--comment=")
        	comment = arg.substr(10,arg.length());
        else if (arg.substr(0,10) == "--revert-to-revision-recursively")
        	revert_recursively = true;	
        else
            opArgs.push_back(arg);

		//in the startscript u can have --run, but could do showrevisions
        if (oldOp && oldOp != op && oldOp != opRunComponent)			
            throw UsageError("only one operation may be specified");
    }
    
    //If no username given get it
    if(username == "")
		username = queryCurrentUsername();

    if (!op) throw UsageError("no operation specified");
    
    /* !!! hack */
    store = openStore();

    op(opFlags, opArgs);
}


string programId = "nix-state";