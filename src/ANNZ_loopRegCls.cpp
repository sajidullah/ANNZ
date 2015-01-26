// ===========================================================================================================
// Copyright (C) 2015, Iftach Sadeh
// 
// This file is part of ANNZ.
// ANNZ is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// ===========================================================================================================

// ===========================================================================================================
/**
 * @brief    - Interface for evaluation mode.
 */
// ===========================================================================================================
void ANNZ::Eval() {
// ================
  aLOG(Log::INFO) <<coutWhiteOnBlack<<coutCyan<<" - starting ANNZ::Eval() ... "<<coutDef<<endl;

  if     (glob->GetOptB("doRegression"))     doEvalReg();
  else if(glob->GetOptB("doClassification")) doEvalCls();

  return;
}

// ===========================================================================================================
/**
 * @brief    - Interface for optimization mode.
 */
// ===========================================================================================================
void ANNZ::Optim() {
// =================
  aLOG(Log::INFO) <<coutWhiteOnBlack<<coutBlue<<" - starting ANNZ::Optim() ... "<<coutDef<<endl;
  
  // make sure all MLMs have postTrain trees and combine all these to a single postTrain tree
  makeTreeRegClsAllMLM();

  // perform the optimization
  if(glob->GetOptB("doRegression")) optimReg();
  else                              optimCls();

  return;
}

// ===========================================================================================================
/**
 * @brief    - Loop over all MLMs, and check that each one has its corresponding "postTrain" trees, validate
 *           that these are up to date, and erge them into a single tree.
 * 
 * @details              
 *           - The following checks are performed:
 *             - Each trained MLM must have a "postTrain" tree with the results of the MLM estimator
 *             - Each postTrain tree must have been generated after the training (it is possible that
 *             an MLM was re-trained after the original postTrain tree was created). Otherwise it
 *             needs to be recreated.
 *             - If new cuts or weights have been added (compared to what was defined during training)
 *             than the postTrain tree needs to be recreated.
 *             - In binned-classification, if two PDFs are requested, than the postTrain trees ned to be
 *             recreated with error estimates (these are not computed by default during training).
 *           - Once all postTrain trees for individual MLMs are up to date, they are all merged into a
 *           single tree, which is used for optimization.
 */
// ===========================================================================================================
void  ANNZ::makeTreeRegClsAllMLM() {
// =================================
  aLOG(Log::INFO) <<coutWhiteOnBlack<<coutYellow<<" - starting ANNZ::makeTreeRegClsAllMLM() ... "<<coutDef<<endl;

  vector <TString>  optNames;
  vector <int>      nEntriesChainV(2);
  OptMaps           * optMap(NULL);
  TChain            * aChain(NULL);
  TString           inTreeName(""), inFileName(""), MLMname(""), treeNamePostfix(""), saveFileName(""), postTrainDirNameMLM(""), outFileName("");
  int               nFilesFound(0), nEntriesChain(0);
  double            timeDiff(0);

  int     nMLMs             = glob->GetOptI("nMLMs");
  TString postTrainDirName  = glob->GetOptC("postTrainDirNameFull");
  bool    separateTestValid = glob->GetOptB("separateTestValid");
  int     maxTreesMerge     = glob->GetOptI("maxTreesMerge");
  bool    needBinClsErr     = glob->GetOptB("needBinClsErr");

  // -----------------------------------------------------------------------------------------------------------
  // get the number of entries in the input trees to compare to the generated result-trees
  // -----------------------------------------------------------------------------------------------------------
  for(int nTrainValidNow=0; nTrainValidNow<2; nTrainValidNow++) {
    if(separateTestValid && (nTrainValidNow==0)) continue;

    treeNamePostfix = (TString)( (nTrainValidNow == 0) ? "_train" : "_valid" );
    inTreeName      = (TString)glob->GetOptC("treeName")+treeNamePostfix;
    inFileName      = (TString)glob->GetOptC("inputTreeDirName")+inTreeName+"*.root";

    aChain = new TChain(inTreeName,inTreeName); aChain->SetDirectory(0); aChain->Add(inFileName); 
    nEntriesChainV[nTrainValidNow] = aChain->GetEntries();
    
    aLOG(Log::DEBUG) <<coutRed<<"Got chain "<<coutGreen<<inTreeName<<"("<<nEntriesChainV[nTrainValidNow]
                     <<")"<<" from "<<coutBlue<<inFileName<<coutDef<<endl;
    
    DELNULL(aChain);
  }

  // -----------------------------------------------------------------------------------------------------------
  // first test of the result-trees exist, if they have the correct number of entries and if they are up to date
  // -----------------------------------------------------------------------------------------------------------
  for(int nMLMnow=0; nMLMnow<nMLMs; nMLMnow++) {
    for(int nCheckNow=0; nCheckNow<2; nCheckNow++) {
      MLMname             = getTagName(nMLMnow);  if(mlmSkip[MLMname]) continue;
      postTrainDirNameMLM = getKeyWord(MLMname,"postTrain","postTrainDirName");

      if(nCheckNow > 0) {
        aLOG(Log::INFO) <<coutRed<<MLMname<<coutYellow<<" - There was need to regenerate the result-trees. "
                        <<"Will validate that all is good now ..."<<coutDef<<endl;
      }

      // -----------------------------------------------------------------------------------------------------------
      // check existance and number of entries of result-trees
      // -----------------------------------------------------------------------------------------------------------
      bool foundGoodTrees = true;
      for(int nTrainValidNow=0; nTrainValidNow<2; nTrainValidNow++) {
        if(separateTestValid && (nTrainValidNow==0)) continue;

        treeNamePostfix = (TString)( (nTrainValidNow == 0) ? "_train" : "_valid" );
        inTreeName      = (TString)glob->GetOptC("treeName")+treeNamePostfix;
        inFileName      = (TString)postTrainDirNameMLM+inTreeName+"*.root";

        aChain          = new TChain(inTreeName,inTreeName); aChain->SetDirectory(0);
        nFilesFound     = aChain->Add(inFileName);
        nEntriesChain   = aChain->GetEntries();

        // if required to generate errors for binned classification, check if the corresponding branch already
        // exists in the chain (it is not created during training...)
        // -----------------------------------------------------------------------------------------------------------
        if(needBinClsErr && nCheckNow == 0) {
          TString MLMname_e   = getTagError(nMLMnow);
          TBranch * errBranch = aChain->GetBranch(MLMname_e);
          
          if(!errBranch) {
            foundGoodTrees = false;
            aLOG(Log::INFO) <<coutRed<<MLMname<<coutYellow<<" - requested 2 binCls PDFs. Will regenerate, as error estimates are needed ..."<<coutDef<<endl;
          }
        }

        DELNULL(aChain);

        if(nFilesFound == 0 || nEntriesChain != nEntriesChainV[nTrainValidNow]) foundGoodTrees = false;

        if(!foundGoodTrees || inLOG(Log::DEBUG_2)) {
          aLOG(Log::INFO) <<coutYellow<<" - "<<coutRed<<MLMname<<coutYellow<<" Now in nTrainValidNow = "<<coutGreen<<nTrainValidNow<<coutYellow
                          <<" , treeNamePostfix = "<<coutGreen<<treeNamePostfix<<coutYellow<<" , number of entries is "
                          <<(foundGoodTrees?(TString)coutGreen+"good":(TString)coutRed+"bad")<<coutYellow<<" ... "<<coutDef<<endl;
        }
        if(!foundGoodTrees) break;
      }

      // -----------------------------------------------------------------------------------------------------------
      // check date of creation (compared to the MLM creation) of the result-trees and the consistency between
      // the cuts, weights and input-errors defined during the production and now
      // -----------------------------------------------------------------------------------------------------------
      if(foundGoodTrees) {
        saveFileName = getKeyWord(MLMname,"postTrain","configSaveFileName"); // saveFileName = (TString)postTrainDirNameMLM+"saveTime.txt";
        optMap       = new OptMaps("localOptMap");

        TString saveName(""); optNames.clear();
        saveName = "userCuts_train";    optNames.push_back(saveName); optMap->NewOptC(saveName);
        saveName = "userCuts_valid";    optNames.push_back(saveName); optMap->NewOptC(saveName);
        saveName = "userWeights_train"; optNames.push_back(saveName); optMap->NewOptC(saveName);
        saveName = "userWeights_valid"; optNames.push_back(saveName); optMap->NewOptC(saveName);

        utils->optToFromFile(&optNames,optMap,saveFileName,"READ","SILENT_KeepFile");
        
        time_t treeTime = static_cast<time_t>(optMap->GetOptF(glob->GetOptC("aTimeName")));
        time_t timeDiff = difftime(trainTimeM[nMLMnow],treeTime);

        TString cuts_train    = optMap->GetOptC("userCuts_train");    TString cuts_valid    = optMap->GetOptC("userCuts_valid");
        TString weights_train = optMap->GetOptC("userWeights_train"); TString weights_valid = optMap->GetOptC("userWeights_valid");

        TString misMatchs("");
        if(cuts_train    != (TString)(getTrainTestCuts("_comn",nMLMnow)+getTrainTestCuts(MLMname+"_train",nMLMnow))) misMatchs += "cuts(train) ";
        if(cuts_valid    != (TString)(getTrainTestCuts("_comn",nMLMnow)+getTrainTestCuts(MLMname+"_valid",nMLMnow))) misMatchs += "cuts(valid) ";
        if(weights_train != userWgtsM[MLMname+"_train"]                                                            ) misMatchs += "weights(train) ";
        if(weights_valid != userWgtsM[MLMname+"_valid"]                                                            ) misMatchs += "weights(valid) ";

        aLOG(Log::DEBUG_2) <<"cuts_train    "<<cuts_train   <<CT<<(TString)(getTrainTestCuts("_comn",nMLMnow)+getTrainTestCuts(MLMname+"_train",nMLMnow))<<endl;
        aLOG(Log::DEBUG_2) <<"cuts_valid    "<<cuts_valid   <<CT<<(TString)(getTrainTestCuts("_comn",nMLMnow)+getTrainTestCuts(MLMname+"_valid",nMLMnow))<<endl;
        aLOG(Log::DEBUG_2) <<"weights_train "<<weights_train<<CT<<userWgtsM[MLMname+"_train"]                                                            <<endl;
        aLOG(Log::DEBUG_2) <<"weights_valid "<<weights_valid<<CT<<userWgtsM[MLMname+"_valid"]                                                            <<endl;

        DELNULL(optMap);

        if(timeDiff > 0) {
          foundGoodTrees = false;
          aLOG(Log::INFO) <<coutRed<<MLMname<<coutYellow<<" - found old result-trees. Will regenerate ..."<<coutDef<<endl;
        }

        if(misMatchs != "") {
          foundGoodTrees = false;
          aLOG(Log::INFO) <<coutRed<<MLMname<<coutYellow<<" - found result-trees with difference in [ "<<coutBlue<<misMatchs<<coutYellow<<" ] ..."<<coutDef<<endl;
        }
      }

      // -----------------------------------------------------------------------------------------------------------
      // generate the trees if needed
      // -----------------------------------------------------------------------------------------------------------
      if(!foundGoodTrees) {
        makeTreeRegClsOneMLM(nMLMnow);
       
        VERIFY(LOCATION,(TString)"Could not generate reg/cls trees succesfully... Something is horribly wrong !!!",(nCheckNow == 0));
      }
      else break;
    }
  }

  // -----------------------------------------------------------------------------------------------------------
  // check if the merged chains already exist and have objects. Then, compare the time they were created at
  // to the time of the result-trees. If any result-trees is newer than the merged chains, recreate the chains.
  // -----------------------------------------------------------------------------------------------------------
  for(int nCheckNow=0; nCheckNow<2; nCheckNow++) {
    if(nCheckNow > 0) {
      aLOG(Log::INFO) <<coutYellow<<" - There was need to regenerate the merged result-trees. Will validate that all is good now ..."<<coutDef<<endl;
    }

    int  hasFound         = 0;
    bool needToMergeTrees = false;
    for(int nTrainValidNow=0; nTrainValidNow<2; nTrainValidNow++) {
      if(separateTestValid && (nTrainValidNow==0)) { hasFound++; continue; }

      treeNamePostfix = (TString)( (nTrainValidNow == 0) ? "_train" : "_valid" );
      inTreeName      = (TString)glob->GetOptC("treeName")+treeNamePostfix;
      inFileName      = (TString)glob->GetOptC("postTrainDirNameFull")+inTreeName+"*.root";

      aChain          = new TChain(inTreeName,inTreeName); aChain->SetDirectory(0);
      nFilesFound     = aChain->Add(inFileName);
      nEntriesChain   = aChain->GetEntries();

      DELNULL(aChain);

      if(nFilesFound > 0 && nEntriesChain == nEntriesChainV[nTrainValidNow]) hasFound++;
    }

    if(hasFound < 2) {
      // checked existance / number of entries
      needToMergeTrees = true;
      aLOG(Log::INFO) <<coutBlue<<" - Did not find requred merged result-trees with the correct number of entries -> Will do merging ..."<<coutDef<<endl;
    }
    else {
      // check date of creation
      TString saveFileName = getKeyWord("","postTrain","configSaveFileName");  //saveFileName = (TString)glob->GetOptC("postTrainDirNameFull")+"saveTime.txt";

      aLOG(Log::INFO) <<coutBlue<<" - Found all requred post-train root files -> Getting from file "<<coutYellow<<saveFileName<<coutBlue
                      <<" the creation time of the merged trees and comparing to the creationg time of source result-trees ..."<<coutDef<<endl;

      optMap = new OptMaps("localOptMap"); optNames.clear();
      utils->optToFromFile(&optNames,optMap,saveFileName,"READ","SILENT_KeepFile");

      time_t mergedTreeTime = static_cast<time_t>(optMap->GetOptF(glob->GetOptC("aTimeName")));

      for(int nMLMnow=0; nMLMnow<nMLMs; nMLMnow++) {
        MLMname             = getTagName(nMLMnow); if(mlmSkip[MLMname]) continue;
        // postTrainDirNameMLM = getKeyWord(MLMname,"postTrain","postTrainDirName");
        saveFileName        = getKeyWord(MLMname,"postTrain","configSaveFileName");  //saveFileName = (TString)postTrainDirNameMLM+"saveTime.txt";

        optMap->clearAll(); optNames.clear();
        utils->optToFromFile(&optNames,optMap,saveFileName,"READ","SILENT_KeepFile");

        time_t treeTime = static_cast<time_t>(optMap->GetOptF(glob->GetOptC("aTimeName")));
        
        timeDiff = difftime(treeTime,mergedTreeTime);
        if(timeDiff > 0) {
          needToMergeTrees = true;
          aLOG(Log::INFO)<<coutGreen<<" - New result-trees found ... will generate merged trees."<<coutDef<<endl;
          break;
        }
      }
      if(!needToMergeTrees) aLOG(Log::INFO)<<coutGreen<<" - No new MLMs found ... no need to regenerate the merged trees."<<coutDef<<endl;

      DELNULL(optMap);
    }

    // merge trees
    if(needToMergeTrees) {
      VERIFY(LOCATION,(TString)"Could not generate reg/cls trees succesfully... Something is horribly wrong !!!",(nCheckNow == 0));

      outputs->InitializeDir(glob->GetOptC("postTrainDirNameFull"),glob->GetOptC("baseName"));
      saveFileName = getKeyWord("","postTrain","configSaveFileName");  //saveFileName = (TString)glob->GetOptC("postTrainDirNameFull")+"saveTime.txt";

      for(int nTrainValidNow=0; nTrainValidNow<2; nTrainValidNow++) {
        if(separateTestValid && (nTrainValidNow==0)) continue;

        treeNamePostfix = (TString)( (nTrainValidNow == 0) ? "_train" : "_valid" );
        inTreeName      = (TString)glob->GetOptC("treeName")+treeNamePostfix;

        // -----------------------------------------------------------------------------------------------------------
        // merge all postTrain trees into one file - if there are too many separate inputs (more than maxTreesMerge)
        // then split the merging into several steps - this avoids situations in which too many input files
        // need to be opened at once
        // -----------------------------------------------------------------------------------------------------------
        // find the names of all postTrain files to merge
        // -----------------------------------------------------------------------------------------------------------
        vector <TString> allFriendFileNameV, outDirNameV, mergeFileNameV;
        
        for(int nMLMnow=0; nMLMnow<nMLMs; nMLMnow++) {
          MLMname             = getTagName(nMLMnow); if(mlmSkip[MLMname]) continue;
          postTrainDirNameMLM = getKeyWord(MLMname,"postTrain","postTrainDirName");
          inFileName          = (TString)postTrainDirNameMLM+inTreeName+"*.root";

          allFriendFileNameV.push_back(inFileName);
        }

        int nFriends    = (int)allFriendFileNameV.size();
        int nMergeLoops = static_cast<int>(ceil(nFriends/double(maxTreesMerge)));

        // -----------------------------------------------------------------------------------------------------------
        // if the number of trees to merge is smaller than maxTreesMerge, just merge all in one go
        // -----------------------------------------------------------------------------------------------------------
        if(nMergeLoops == 1) mergeFileNameV = allFriendFileNameV;
        // -----------------------------------------------------------------------------------------------------------
        // if more than one iteration of merging is needed, merge in steps into new subdirectories
        // -----------------------------------------------------------------------------------------------------------
        else {
          mergeFileNameV.clear();

          aLOG(Log::INFO)<<coutGreen<<" - Found "<<coutBlue<<nFriends<<coutGreen<<" MLM trees to merge, but have maxTreesMerge = "
                         <<coutBlue<<maxTreesMerge<<coutGreen<<" -> Will merge in "<<coutYellow<<nMergeLoops<<coutGreen<<" sub-steps"<<coutDef<<endl;

          for(int nMergeLoopNow=0; nMergeLoopNow<nMergeLoops; nMergeLoopNow++) {
            aLOG(Log::INFO)<<coutGreen<<" - Now in merging loop "<<coutPurple<<nMergeLoopNow+1<<coutGreen<<"/"<<coutPurple<<nMergeLoops<<coutDef<<endl;

            int nFileMin = maxTreesMerge * nMergeLoopNow;
            int nFileMax = min(maxTreesMerge * (nMergeLoopNow + 1) , nFriends);

            TString outDirNameOrig = (TString)outputs->GetOutDirName();
            TString outDirNameNew  = (TString)outDirNameOrig+TString::Format("nMerge_%d",nMergeLoopNow)+"/";
            outputs->InitializeDir(outDirNameNew,glob->GetOptC("baseName"));  outDirNameV.push_back(outDirNameNew);

            aChain      = new TChain(inTreeName,inTreeName); aChain->SetDirectory(0);            
            outFileName = (TString)outDirNameNew+inTreeName+"*.root";  mergeFileNameV.push_back(outFileName);

            vector <TString> mergeFileNameNowV;
            for(int nFileNow=nFileMin; nFileNow<nFileMax; nFileNow++) {
              if(nFileNow == nFileMin) aChain->Add(allFriendFileNameV[nFileNow]);
              else                     mergeFileNameNowV.push_back(allFriendFileNameV[nFileNow]);
            }

            TChain * aChainMerged = mergeTreeFriends(aChain,NULL,&mergeFileNameNowV);
            
            DELNULL(aChain); DELNULL(aChainMerged); mergeFileNameNowV.clear();
            outputs->SetOutDirName(outDirNameOrig);
          }

          aLOG(Log::INFO)<<coutGreen<<" - Now in final merging of all sub-steps ..."<<coutDef<<endl;
        }
        VERIFY(LOCATION,(TString)"Found no postTrain chain to merge ... Something is horribly wrong !!!",((int)mergeFileNameV.size() > 0));

        // -----------------------------------------------------------------------------------------------------------
        // now merge all the trees into the final product
        // -----------------------------------------------------------------------------------------------------------
        aChain = new TChain(inTreeName,inTreeName); aChain->SetDirectory(0);

        aChain->Add(mergeFileNameV[0]);
        mergeFileNameV.erase(mergeFileNameV.begin());

        TChain * aChainMerged = mergeTreeFriends(aChain,NULL,&mergeFileNameV);
        
        // validate that all objects are consistent (same index parameter accross tree friends)
        verifyIndicesMLM(aChainMerged);
        
        // cleanup
        DELNULL(aChain); DELNULL(aChainMerged); mergeFileNameV.clear();

        for(int nOutDirNameNow=0; nOutDirNameNow<(int)outDirNameV.size(); nOutDirNameNow++) {
          if(!inLOG(Log::DEBUG_1)) utils->safeRM(outDirNameV[nOutDirNameNow],inLOG(Log::DEBUG));
        }
        outDirNameV.clear(); allFriendFileNameV.clear();
      }

      // 
      aLOG(Log::INFO)<<coutYellow<<" - Saving file "<<coutGreen<<saveFileName<<coutYellow<<" to log the creation time of the trees ..."<<coutDef<<endl;

      optMap = new OptMaps("localOptMap"); optNames.clear();
      utils->optToFromFile(&optNames,optMap,saveFileName,"WRITE");

      DELNULL(optMap);
    }
    else break;
  }

  nEntriesChainV.clear(); optNames.clear();

  // make sure we are back in the correct working directory
  outputs->SetOutDirName(glob->GetOptC("outDirNameFull"));
  
  aLOG(Log::INFO) <<coutWhiteOnBlack<<coutGreen<<" - ending makeTreeRegClsAllMLM() ... "<<coutDef<<endl;

  return;
}


// ===========================================================================================================
/**
 * @brief          - Create a "postTrain" tree for a given MLM, which includes the result of the MLM estimator.
 *
 * @param nMLMnow  - The index of the primary MLM.
 */
// ===========================================================================================================
void  ANNZ::makeTreeRegClsOneMLM(int nMLMnow) {
// ============================================
  aLOG(Log::INFO) <<coutWhiteOnBlack<<coutPurple<<" - starting ANNZ::makeTreeRegClsOneMLM() - "
                  <<"will create postTrain trees for "<<coutGreen<<getTagName(nMLMnow)<<coutPurple<<" ... "<<coutDef<<endl;
  
  int     maxNobj           = 0;  // maxNobj = glob->GetOptI("maxNobj"); // only allow limits in case of debugging !! 
  TString indexName         = glob->GetOptC("indexName");
  TString isSigName         = glob->GetOptC("isSigName");
  TString testValidType     = glob->GetOptC("testValidType");
  UInt_t  seed              = glob->GetOptI("initSeedRnd") * 58606;
  bool    separateTestValid = glob->GetOptB("separateTestValid");
  bool    isCls             = glob->GetOptB("doClassification") || glob->GetOptB("doBinnedCls");
  bool    needBinClsErr     = glob->GetOptB("needBinClsErr");
  int     nMLMs             = glob->GetOptI("nMLMs");

  bool    isErrKNN          = !isCls || needBinClsErr;
  bool    isErrINP          = false;
  TString MLMname           = getTagName(nMLMnow);
  TString MLMname_eN        = getTagError(nMLMnow,"N");
  TString MLMname_e         = getTagError(nMLMnow,"");
  TString MLMname_eP        = getTagError(nMLMnow,"P");
  TString MLMname_w         = getTagWeight(nMLMnow);
  TString MLMname_v         = getTagClsVal(nMLMnow);
  TString MLMname_i         = getTagIndex(nMLMnow);

  ANNZ_readType aReadType   = isCls ? ANNZ_readType::PRB : ANNZ_readType::REG;

  // this check is safe, since (inNamesErr[nMLMnow].size() > 0) was confirmed in setNominalParams()
  VERIFY(LOCATION,(TString)"inNamesErr["+utils->intToStr(nMLMnow)+"] not initialized... something is horribly wrong ?!?",(inNamesErr[nMLMnow].size() > 0));
  if(isErrKNN && (inNamesErr[nMLMnow][0] != "")) { isErrKNN = false; isErrINP = true; }

  if(isErrKNN) aLOG(Log::INFO)<<coutYellow<<" - Will gen. errors by KNN method ..."<<coutDef<<endl;
  if(isErrINP) aLOG(Log::INFO)<<coutYellow<<" - Will gen. input-parameter errors ..."<<coutDef<<endl;

  TString postTrainDirName  = getKeyWord(MLMname,"postTrain","postTrainDirName");
  TString saveFileName      = getKeyWord(MLMname,"postTrain","configSaveFileName");

  // set the output directory to the postTrainDirName dir
  outputs->InitializeDir(postTrainDirName,glob->GetOptC("baseName"));

  // set the current MLM as accepted and load the corresponding reader
  map <TString,bool> mlmSkipNow;
  for(int nMLMnow0=0; nMLMnow0<nMLMs; nMLMnow0++) {
    TString    MLMname  = getTagName(nMLMnow0);
    mlmSkipNow[MLMname] = (nMLMnow0 != nMLMnow);
  }
  loadReaders(mlmSkipNow);

  mlmSkipNow.clear();

  double separation(-1);
  for(int nTrainValidNow=0; nTrainValidNow<2; nTrainValidNow++) {
    if(separateTestValid && (nTrainValidNow==0)) continue;

    TString treeNamePostfix = (TString)( (nTrainValidNow == 0) ? "_train" : "_valid" );
    TString baseCutsName    = (TString)"_comn"+";"+MLMname+treeNamePostfix;

    aLOG(Log::DEBUG_1) <<coutYellow<<" - Now in nTrainValidNow = "<<coutGreen<<nTrainValidNow<<coutYellow<<" , treeNamePostfix = "
                       <<coutGreen<<treeNamePostfix<<coutYellow<<" ... "<<coutDef<<endl;

    // 
    // -----------------------------------------------------------------------------------------------------------  
    VarMaps * varKNN(NULL);        TChain        * aChainKnn(NULL);
    TFile   * knnErrOutFile(NULL); TMVA::Factory * knnErrFactory(NULL); TMVA::kNN::ModulekNN * knnErrModule(NULL);

    if(isErrKNN) {
      // -----------------------------------------------------------------------------------------------------------  
      // the _train trees are used in all cases:
      //  - if (separateTestValid == false) then the errors of the _train will be derived from the same source, but
      //    there is no choice in the matter anyway...
      //  - if (separateTestValid == true) then the errors for both the testing (ANNZ_tvType<0.5 in _valid) and for the
      //    validation (ANNZ_tvType>0.5 in _valid) will be derived from the independent source of the _train tree
      // -----------------------------------------------------------------------------------------------------------  
      TString inTreeNameKnn = (TString)glob->GetOptC("treeName")+"_train";
      TString inFileNameKnn = (TString)glob->GetOptC("inputTreeDirName")+inTreeNameKnn+"*.root";

      aChainKnn = new TChain(inTreeNameKnn,inTreeNameKnn); aChainKnn->SetDirectory(0); aChainKnn->Add(inFileNameKnn);
      int nEntriesChainKnn = aChainKnn->GetEntries();
      aLOG(Log::DEBUG) <<coutRed<<" - Created KnnErr chain  "<<coutGreen<<inTreeNameKnn
                       <<"("<<nEntriesChainKnn<<")"<<" from "<<coutBlue<<inFileNameKnn<<coutDef<<endl;

      varKNN = new VarMaps(glob,utils,"varKNN");
      varKNN->connectTreeBranches(aChainKnn);  // connect the tree so as to allocate memory for cut variables

      setMethodCuts(varKNN,nMLMnow);

      TCut    cutsNow(varKNN->getTreeCuts("_comn") + varKNN->getTreeCuts(MLMname+treeNamePostfix)), cutsSig(""), cutsBck("");
      TString wgtReg(userWgtsM[MLMname+treeNamePostfix]), wgtSig("1"), wgtBck("1");

      if(needBinClsErr) { cutsSig = userCutsM[MLMname+"_sig"]; cutsBck = userCutsM[MLMname+"_bck"]; }

      setupKdTreeKNN(aChainKnn,cutsNow,nMLMnow,knnErrOutFile,knnErrFactory,knnErrModule,cutsSig,cutsBck,wgtReg,wgtSig,wgtBck);
    }

    // -----------------------------------------------------------------------------------------------------------
    // now create the _valid tree
    // -----------------------------------------------------------------------------------------------------------
    VarMaps * var_0 = new VarMaps(glob,utils,"treeRegClsVar_0");
    VarMaps * var_1 = new VarMaps(glob,utils,"treeRegClsVar_1");

    // create the chain for the loop
    // -----------------------------------------------------------------------------------------------------------
    TString inTreeName = (TString)glob->GetOptC("treeName")+treeNamePostfix;
    TString inFileName = (TString)glob->GetOptC("inputTreeDirName")+inTreeName+"*.root";

    // prepare the chain and input variables. Set cuts to match the TMVAs
    // -----------------------------------------------------------------------------------------------------------
    TChain * aChain = new TChain(inTreeName,inTreeName); aChain->SetDirectory(0); aChain->Add(inFileName); 
    int nEntriesChain = aChain->GetEntries();
    aLOG(Log::INFO) <<coutRed<<" - added chain "<<coutGreen<<inTreeName<<"("<<nEntriesChain<<")"<<" from "<<coutBlue<<inFileName<<coutDef<<endl;

    var_1->NewVarI(testValidType);

    // create MLM-weight formulae for the input variables
    var_0->NewForm(MLMname_w,userWgtsM[MLMname+treeNamePostfix]);

    // formulae for inpput-variable errors, to be used by getRegClsErrINP()
    if(isErrINP) {
      int nInErrs = (int)inNamesErr[nMLMnow].size();
      for(int nInErrNow=0; nInErrNow<nInErrs; nInErrNow++) {
        TString inVarErr = getTagInVarErr(nMLMnow,nInErrNow);

        var_0->NewForm(inVarErr,inNamesErr[nMLMnow][nInErrNow]);
      }
    }

    // create MLM, MLM-eror and MLM-weight variables for the output vars
    var_1->NewVarF(MLMname); var_1->NewVarF(MLMname_w); var_1->NewVarI(MLMname_i);
    if(isCls)                   { var_1->NewVarF(MLMname_v); var_1->NewVarB(isSigName); }
    if(!isCls || needBinClsErr) { var_1->NewVarF(MLMname_eN); var_1->NewVarF(MLMname_e); var_1->NewVarF(MLMname_eP);}

    // setup cuts for the vars we loop on for sig/bck determination in case of classification
    setMethodCuts(var_0,nMLMnow);

    // connect the input vars to the tree before looping
    // -----------------------------------------------------------------------------------------------------------
    var_0->connectTreeBranchesForm(aChain,&readerInptV);

    // create the output tree and connect it to the output vars
    // -----------------------------------------------------------------------------------------------------------
    inTreeName = (TString)glob->GetOptC("treeName")+treeNamePostfix;
    TTree * treeOut = new TTree(inTreeName,inTreeName); treeOut->SetDirectory(0);
    outputs->TreeMap[inTreeName] = treeOut;
    var_1->createTreeBranches(treeOut); 

    // -----------------------------------------------------------------------------------------------------------
    // loop on the tree
    // -----------------------------------------------------------------------------------------------------------
    vector <double> regErrV(3,0);

    bool  breakLoop(false), mayWriteObjects(false);
    int   nObjectsToWrite(glob->GetOptI("nObjectsToWrite")), nObjectsToPrint(glob->GetOptI("nObjectsToPrint"));
    var_0->clearCntr();
    for(Long64_t loopEntry=0; true; loopEntry++) {
      if(!var_0->getTreeEntry(loopEntry)) breakLoop = true;

      if((var_0->GetCntr("nObj") % nObjectsToPrint == 0 && var_0->GetCntr("nObj") > 0) || breakLoop) { var_0->printCntr(inTreeName); }
      if((mayWriteObjects && var_0->GetCntr("nObj") % nObjectsToWrite == 0) || breakLoop) {
        outputs->WriteOutObjects(false,true); outputs->ResetObjects(); mayWriteObjects = false;
      }
      if(breakLoop) break;

      // set to default before anything else
      var_1->setDefaultVals();

      // check if passed cuts ("_comn" , and (MLMname+treeNamePostfix))
      bool passCuts    = !var_0->hasFailedTreeCuts(baseCutsName);
      // only need to compute the error if this we are in _valid object and have passed the cuts
      bool isErrKNNnow = isErrKNN && passCuts;
      bool isErrINPnow = isErrINP && passCuts;

      var_0->IncCntr("nObj");

      bool skipObj(false), isBck(false);
      if(isCls) {
        isBck = var_0->hasFailedTreeCuts("_sig");

        TString sigBckName = (TString)(isBck ? "nObj_bck" : "nObj_sig");
        if(var_0->GetCntr(sigBckName) > 0 && var_0->GetCntr(sigBckName) == maxNobj) skipObj = true;
        var_0->IncCntr(sigBckName+"_loop");

        var_1->SetVarB(isSigName,!isBck);
      }
      if(skipObj) continue; // only relevant for classification
      
      var_1->SetVarI(MLMname_i,    var_0->GetVarI(indexName)    );
      var_1->SetVarI(testValidType,var_0->GetVarI(testValidType));

      // fill the output tree
      if(isCls) {
        double  clasVal = getReader(var_0,ANNZ_readType::CLS,true ,nMLMnow);
        double  clsPrb  = getReader(var_0,ANNZ_readType::PRB,false,nMLMnow);
        double  clsWgt  = var_0->GetForm(MLMname_w) * (passCuts?1:0);

        // sanity check that weights are properly defined
        if(clsWgt < 0) { var_0->printVars(); VERIFY(LOCATION,(TString)"Weights can only be >= 0 ... Something is horribly wrong ?!?",false); }

        var_1->SetVarF(MLMname_v,clasVal); var_1->SetVarF(MLMname,clsPrb); var_1->SetVarF(MLMname_w,clsWgt);
      }
      else {
        double  regVal  = getReader(var_0,ANNZ_readType::REG,true ,nMLMnow);
        double  regWgt  = var_0->GetForm(MLMname_w) * (passCuts?1:0);

        // sanity check that weights are properly defined
        if(regWgt < 0) { var_0->printVars(); VERIFY(LOCATION,(TString)"Weights can only be >= 0 ... Something is horribly wrong ?!?",false); }

        var_1->SetVarF(MLMname,regVal); var_1->SetVarF(MLMname_w,regWgt);
      }

      if(!isCls || needBinClsErr) {
        if     (isErrKNNnow) getRegClsErrKNN(var_0,aReadType,nMLMnow,knnErrModule,&regErrV);
        else if(isErrINPnow) getRegClsErrINP(var_0,aReadType,nMLMnow,&seed,       &regErrV);

        var_1->SetVarF(MLMname_eN,regErrV[0]);
        var_1->SetVarF(MLMname_e ,regErrV[1]);
        var_1->SetVarF(MLMname_eP,regErrV[2]);
      }

      treeOut->Fill(); //var_0->printVars(); cout <<"----------------------"<<endl; var_1->printVars();

      // to increment the loop-counter, at least one method should have passed the cuts
      mayWriteObjects = true;
      if(isCls) {
        if(isBck) var_0->IncCntr("nObj_bck"); else var_0->IncCntr("nObj_sig");
        if(var_0->GetCntr("nObj_sig") == maxNobj && var_0->GetCntr("nObj_bck") == maxNobj) breakLoop = true;
      }
      else {
        if(var_0->GetCntr("nObj") == maxNobj) breakLoop = true;
      }  
    //cout <<skipObj<<CT<<var_0->GetCntr("nObj") <<CT<< var_0->GetCntr("nObj_sig") <<CT<< var_0->GetCntr("nObj_bck") <<endl;
    }
    if(!breakLoop) { var_0->printCntr(inTreeName); outputs->WriteOutObjects(false,true); outputs->ResetObjects(); }

    regErrV.clear();

    // -----------------------------------------------------------------------------------------------------------
    // for classification, get the separation parameter
    // -----------------------------------------------------------------------------------------------------------
    if(isCls) {
      // create the chain from the output which has just been created
      TString outFileName = (TString)postTrainDirName+inTreeName+"*.root";

      // prepare the chain and input variables. Set cuts to match the TMVAs
      TChain * aChainOut = new TChain(inTreeName,inTreeName); aChainOut->SetDirectory(0); aChainOut->Add(outFileName); 
      aLOG(Log::DEBUG) <<coutRed<<" - added chain "<<coutGreen<<inTreeName<<"("<<aChainOut->GetEntries()<<")"<<" from "<<coutBlue<<outFileName<<coutDef<<endl;

      TH1 * his1_sig(NULL), * his1_bck(NULL);
      for(int nSigBckNow=0; nSigBckNow<2; nSigBckNow++) {
        TString sigBckName = (TString)((nSigBckNow == 0) ? "_sig" : "_bck");
        TString sigBckCut  = (TString)((nSigBckNow == 0) ? ">0.5" : "<0.5");
        
        TString hisName    = (TString)"sepHis"+sigBckName;
        TH1     * his1_sb  = new TH1D(hisName,hisName,100,0,1); 
        TString cutExprs   = (TString)"("+MLMname_w+" > 0) && ("+isSigName+sigBckCut+") && ("+(TString)var_0->getTreeCuts("_train")+")";
        TString drawExprs  = (TString)MLMname+">>+"+hisName;
        
        TCanvas * tmpCnvs  = new TCanvas("tmpCnvs","tmpCnvs");
        int     nEvtPass   = aChainOut->Draw(drawExprs,cutExprs); DELNULL(tmpCnvs);

        if(nEvtPass > 0) {
          his1_sb->SetDirectory(0); // allowed only after the chain fills the histogram
          if(nSigBckNow == 0) his1_sig = his1_sb;
          else                his1_bck = his1_sb;
        }
        else DELNULL(his1_sb);
      }

      if(his1_sig && his1_bck) {
        separation = getSeparation(his1_sig,his1_bck);

        aLOG(Log::INFO)<<coutYellow<<" - Got separation parameter: "<<coutGreen<<separation<<coutDef<<endl;
      }

      DELNULL(aChainOut); DELNULL(his1_sig); DELNULL(his1_bck);
    }

    //cleanup
    DELNULL(var_0); DELNULL(var_1); DELNULL(aChain);

    if(isErrKNN) {
      DELNULL(varKNN); cleanupKdTreeKNN(knnErrOutFile,knnErrFactory); DELNULL(aChainKnn);

      utils->safeRM(getKeyWord(MLMname,"knnErrXML","outFileDirKnnErr"), inLOG(Log::DEBUG));
      utils->safeRM(getKeyWord(MLMname,"knnErrXML","outFileNameKnnErr"),inLOG(Log::DEBUG));
    }
  }

  // re-set the output directory to the correct path
  outputs->SetOutDirName(glob->GetOptC("outDirNameFull"));

  // 
  aLOG(Log::INFO)<<coutYellow<<" - Saving file "<<coutGreen<<saveFileName<<coutYellow<<" to log the creation time of the trees, and"
                 <<" the user-defined cuts and weights for _train, _valid ..."<<coutDef<<endl;

  OptMaps * optMap = new OptMaps("localOptMap");

  TString saveName  = "";
  TString cut_train = (TString)(getTrainTestCuts("_comn",nMLMnow)+getTrainTestCuts(MLMname+"_train",nMLMnow));
  TString cut_valid = (TString)(getTrainTestCuts("_comn",nMLMnow)+getTrainTestCuts(MLMname+"_valid",nMLMnow));
  
  vector <TString> optNames;
  saveName = "userCuts_train";    optNames.push_back(saveName); optMap->NewOptC(saveName, cut_train);
  saveName = "userCuts_valid";    optNames.push_back(saveName); optMap->NewOptC(saveName, cut_valid);
  saveName = "userWeights_train"; optNames.push_back(saveName); optMap->NewOptC(saveName, userWgtsM[MLMname+"_train"]);
  saveName = "userWeights_valid"; optNames.push_back(saveName); optMap->NewOptC(saveName, userWgtsM[MLMname+"_valid"]);
  saveName = "separation";        optNames.push_back(saveName); optMap->NewOptF(saveName, separation);

  utils->optToFromFile(&optNames,optMap,saveFileName,"WRITE");

  //cleanup
  optNames.clear(); DELNULL(optMap);

  clearReaders();

  aLOG(Log::INFO) <<coutWhiteOnBlack<<coutGreen<<" - ending makeTreeRegClsOneMLM() ... "<<coutDef<<endl;
  return;
}


// ===========================================================================================================
/**
 * @brief                 - Compute the separation parameter between two distributions.
 *
 * @details        
 *                        - see "separation" in: http://root.cern.ch/root/html/ANNZ__MethodBase.html .
 *                        - Uses both a binned estimator and a fitted PDF estimator to compute the separation.
 *                  
 * @param hisSig, hisBck  - Two histograms with the same xAxis bins which contain the distributions for which
 *                        the separation is computed.
 * 
 * @return                - The separation parameter.
 */
// ===========================================================================================================
double ANNZ::getSeparation(TH1 * hisSig, TH1 * hisBck) {
// =====================================================
  VERIFY(LOCATION,(TString)"Memory leak ?! ",(dynamic_cast<TH1*>(hisSig) && dynamic_cast<TH1*>(hisBck)));

  int     width(15);
  double  sbSepFracPdf(0), sbSepFracHis(0), sbSepFracDif(0), maxNomHisDif(0.2);

  double  intgrSig(hisSig->Integral()), intgrBck(hisBck->Integral());
  bool    emptyHis = (intgrSig < EPS || intgrBck < EPS);
  bool    normHis  = (fabs(intgrSig-1) > 1e-10 || fabs(intgrBck-1) > 1e-10);

  if(emptyHis) return 0;

  if(normHis) {
    hisSig = (TH1*)hisSig->Clone((TString)hisSig->GetName()+glob->GetOptC("pdfSepName")+"_cln");  hisSig->Scale(1/intgrSig);
    hisBck = (TH1*)hisBck->Clone((TString)hisBck->GetName()+glob->GetOptC("pdfSepName")+"_cln");  hisBck->Scale(1/intgrBck);
  }

  if(glob->GetOptB("getSeparationWithPDF")) {
    vector <TMVA::PDF*> pdfSepV(2);
    
    for(int nPdfType=0; nPdfType<2; nPdfType++) {
      TH1     * hisSigBck = (nPdfType == 0) ? hisSig : hisBck;
      TString sigBckName  = (TString)( (nPdfType == 0) ? "_sig" : "_bck" );
      TString pdfSepName  = (TString)glob->GetOptC("pdfSepName")+sigBckName;
      TString pdfSepOpt   = glob->GetOptC("pdfSepStr");  pdfSepOpt.ReplaceAll(glob->GetOptC("pdfSepName"),pdfSepName);

      pdfSepV[nPdfType] = new TMVA::PDF(pdfSepName, pdfSepOpt, pdfSepName);
      pdfSepV[nPdfType]->DeclareOptions();  pdfSepV[nPdfType]->ParseOptions();  pdfSepV[nPdfType]->ProcessOptions();
      pdfSepV[nPdfType]->BuildPDF(hisSigBck);
    }

    sbSepFracPdf = TMVA::gTools().GetSeparation(*(pdfSepV[0]),*(pdfSepV[1])); sbSepFracPdf = max(min(sbSepFracPdf,1.),0.);
    sbSepFracHis = TMVA::gTools().GetSeparation(hisSig,hisBck);               sbSepFracHis = max(min(sbSepFracHis,1.),0.);

    // compare the two calculations
    sbSepFracDif = (sbSepFracPdf > EPS) ? fabs(sbSepFracPdf-sbSepFracHis) : maxNomHisDif+0.1;
  
    aLOG(Log::DEBUG) <<coutRed<<" - getSeparation("<<hisSig->GetName()<<") - pdf,his,|pdf-his| :  "
                     <<coutBlue<<std::setw(width)<<std::left<<sbSepFracPdf<<std::setw(width)<<std::left<<sbSepFracHis
                     <<coutYellow<<std::scientific<<std::setw(width)<<std::left<<sbSepFracDif<<std::fixed<<coutDef<<endl;

    // if the difference is too large, don't trust the calculation and return zero separation
    if(sbSepFracDif > maxNomHisDif) sbSepFracPdf = 0;
  
    for(int nPdfType=0; nPdfType<2; nPdfType++) { DELNULL(pdfSepV[nPdfType]); }
    pdfSepV.clear();
  }
  else {
    sbSepFracPdf = TMVA::gTools().GetSeparation(hisSig,hisBck); sbSepFracPdf = max(min(sbSepFracPdf,1.),0.);
  }

  if(normHis) {
    DELNULL(hisSig); DELNULL(hisBck); 
  }

  return sbSepFracPdf;
}

// ===========================================================================================================
/**
 * @brief                       - Merge a pair of input chains, or an input chain with a collection of input files into one tree.
 *
 * @param aChain                - The primary chain.
 * @param aChainFriend          - If given as a valid pointer, this chain will be merged with aChain.
 * @param chainFriendFileNameV  - If [aChainFriend == NULL], chainFriendFileNameV should contain a list of input files
 *                              from which new friend-chains will be created, to be merged with aChain.
 * @param acceptV               - If [acceptV != NULL], it will act as a list of variables which will be added to the merged tree.
 * @param rejectV               - If [rejectV != NULL], it will act as a list of variables which will be excluded from the merged tree.
 * @param aCut                  - If [aCut != ""], it will serve as a cut on objects which will be excluded from the merged tree.
 * 
 * @return                      - The newly-created merged chain.
 */
// ===========================================================================================================
TChain * ANNZ::mergeTreeFriends(TChain * aChain, TChain * aChainFriend, vector<TString> * chainFriendFileNameV,
                                vector <TString> * acceptV, vector <TString> * rejectV, TCut aCut) {
// =================================================================================================
  aLOG(Log::INFO) <<coutWhiteOnBlack<<coutCyan<<" - starting ANNZ::mergeTreeFriends() ... "<<coutDef<<endl;

  VERIFY(LOCATION,(TString)"Memory leak ?! ",(dynamic_cast<TChain*>(aChain)));
  VERIFY(LOCATION,(TString)"aChain seems to be empty... Something is horribly wrong !!!",((int)aChain->GetEntries() > 0));

  vector <TChain *> aChainFriendV;

  TString inTreeName  = (TString)aChain->GetName();
  TString inFileName  = (TString)outputs->GetOutDirName()+inTreeName+"*.root";

  if(dynamic_cast<TChain*>(aChainFriend)) {
    aChain->AddFriend(aChainFriend,utils->nextTreeFriendName(aChain));
  }
  else {
    VERIFY(LOCATION,(TString)"If not providing aChainFriend, must give chainFriendFileNameV",(dynamic_cast<vector<TString>*>(chainFriendFileNameV)));

    aChainFriendV.resize(chainFriendFileNameV->size());
    for(int nFriendNow=0; nFriendNow<(int)chainFriendFileNameV->size(); nFriendNow++) {
      aChainFriendV[nFriendNow] = new TChain(inTreeName,inTreeName); aChainFriendV[nFriendNow]->SetDirectory(0);
      aChainFriendV[nFriendNow]->Add(chainFriendFileNameV->at(nFriendNow));    

      aChain->AddFriend(aChainFriendV[nFriendNow],utils->nextTreeFriendName(aChain));
    }
  }

  VarMaps * var_0 = new VarMaps(glob,utils,(TString)"inputTreeVars_0_"+inTreeName);
  VarMaps * var_1 = new VarMaps(glob,utils,(TString)"inputTreeVars_1_"+inTreeName);

  TTree * mergedTree = new TTree(inTreeName,inTreeName); mergedTree->SetDirectory(0);
  outputs->TreeMap[inTreeName] = mergedTree;

  var_0->connectTreeBranches(aChain);
  var_1->copyVarStruct(var_0,acceptV,rejectV); var_1->createTreeBranches(mergedTree); 

  bool hasCut = (aCut != "");
  if(hasCut) var_0->setTreeCuts("aCut",aCut);

  bool  breakLoop(false), mayWriteObjects(false);
  int   nObjectsToWrite(glob->GetOptI("nObjectsToWrite"));
  var_0->clearCntr();
  for(Long64_t loopEntry=0; true; loopEntry++) {
    if(!var_0->getTreeEntry(loopEntry)) breakLoop = true;

    if((mayWriteObjects && var_0->GetCntr("nObj") % nObjectsToWrite == 0) || breakLoop) {
      var_0->printCntr(inTreeName,Log::DEBUG); outputs->WriteOutObjects(false,true); outputs->ResetObjects();
      mayWriteObjects = false;
    }
    if(breakLoop) break;

    if(hasCut) { if(var_0->hasFailedTreeCuts("aCut")) continue; }

    // set to default before anything else
    var_1->setDefaultVals();
    var_1->copyVarData(var_0);

    mergedTree->Fill();

    var_0->IncCntr("nObj"); mayWriteObjects = true;
  }
  if(!breakLoop) { var_0->printCntr(inTreeName,Log::DEBUG); outputs->WriteOutObjects(false,true); outputs->ResetObjects(); }

  DELNULL(var_0); DELNULL(var_1);

  for(int nFriendNow=0; nFriendNow<(int)aChainFriendV.size(); nFriendNow++) DELNULL(aChainFriendV[nFriendNow]);
  aChainFriendV.clear();

  DELNULL(mergedTree); outputs->TreeMap.erase(inTreeName);

  aLOG(Log::DEBUG) <<coutRed<<"Created new chain (using added friend trees)  "<<coutGreen<<inTreeName
                   <<coutRed<<"  from  "<<coutBlue<<inFileName<<coutDef<<endl;

  TChain * aChainOut = new TChain(inTreeName,inTreeName); aChainOut->SetDirectory(0);  aChainOut->Add(inFileName);

  return aChainOut;
}

// ===========================================================================================================
/**
 * @brief         - Extract the names of all "index" variables from a chain (e.g., those given by getTagIndex()),
 *                and make sure they are the same, lin-by-line. This serves to validate that merged chains are
 *                properly syncorinized.
 *
 * @param aChain  - The input chain.
 */
// ===========================================================================================================
void  ANNZ::verifyIndicesMLM(TChain * aChain) {
// ============================================
  VERIFY(LOCATION,(TString)"Memory leak ?! ",(dynamic_cast<TChain*>(aChain)));
  VERIFY(LOCATION,(TString)"Memory leak ?! ",(dynamic_cast<TFile*>(aChain->GetFile())));
  aLOG(Log::INFO) <<coutWhiteOnBlack<<coutYellow<<" - starting ANNZ::verifyIndicesMLM("<<coutGreen<<aChain->GetName()<<coutYellow<<" , "
                  <<coutGreen<<aChain->GetFile()->GetName()<<coutYellow<<") ... "<<coutDef<<endl;

  // get the list of MLM_index branches from the chain
  // -----------------------------------------------------------------------------------------------------------
  vector <TString> branchNameV, indexNameV;
  utils->getTreeBranchNames(aChain,branchNameV);

  TString indexName    = glob->GetOptC("indexName");
  TString indexTagPatt = getTagIndex(0);
  TString MLMname0     = getTagName(0);
  VERIFY(LOCATION,(TString)"Something wrong with MLMname setup ... \""+indexTagPatt+"\" should contain \""+MLMname0+"\"",(indexTagPatt.Contains(MLMname0)));
  indexTagPatt.ReplaceAll(MLMname0,"");

  TString allIndexNames("");
  for(int nBranchNameNow=0; nBranchNameNow<(int)branchNameV.size(); nBranchNameNow++) {
    TString branchName     = branchNameV[nBranchNameNow];
    bool    needBranchName = (branchName.Contains(indexTagPatt) || branchName == indexName);

    if(needBranchName) {
      indexNameV.push_back(branchName);
      allIndexNames += coutPurple+branchName+coutGreen+",";
    }
  }
  int nIndices = (int)indexNameV.size();

  if(nIndices < 2) {
    aLOG(Log::INFO) <<coutYellow<<" - found only "<<nIndices<<" MLMs in the chain. Nothing to do... "<<coutDef<<endl;
  }
  else {
    aLOG(Log::DEBUG) <<coutGreen<<" - will verify consistancy of the following branchs: "<<allIndexNames<<coutDef<<endl;

    // define a VarMaps to loop over the chain
    // -----------------------------------------------------------------------------------------------------------
    TString aChainName = aChain->GetName();
    VarMaps * var      = new VarMaps(glob,utils,(TString)"inputTreeVars_"+aChainName);
    var->connectTreeBranches(aChain);

    // -----------------------------------------------------------------------------------------------------------
    // loop on the tree
    // -----------------------------------------------------------------------------------------------------------
    bool  breakLoop(false);
    int   nObjectsToWrite(glob->GetOptI("nObjectsToWrite"));
    var->clearCntr();
    for(Long64_t loopEntry=0; true; loopEntry++) {
      if(!var->getTreeEntry(loopEntry)) breakLoop = true;

      if((var->GetCntr("nObj")+1 % nObjectsToWrite == 0) || breakLoop) { var->printCntr(aChainName,Log::DEBUG_1); }
      if(breakLoop) break;
      
      for(int nIndexNow=1; nIndexNow<nIndices; nIndexNow++) {
        bool isDiffIndex = (var->GetVarI(indexNameV[0]) != var->GetVarI(indexNameV[nIndexNow]));
        
        if(isDiffIndex) {
          TString message = (TString)"Found inconsistent MLM indices - ["
                            +indexNameV[0]        +" = "+utils->intToStr(var->GetVarI(indexNameV[0]))
                            +" , "
                            +indexNameV[nIndexNow]+" = "+utils->intToStr(var->GetVarI(indexNameV[nIndexNow]))
                            +"] ... something is horribly wrong ?!? ";
          VERIFY(LOCATION,message,false);
        }
      }

      var->IncCntr("nObj");
    }
    if(!breakLoop) { var->printCntr(aChainName,Log::DEBUG_1); }

    DELNULL(var);
  }

  // cleanup
  branchNameV.clear(); indexNameV.clear();

  return;
}


