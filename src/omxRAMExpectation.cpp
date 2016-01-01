/*
  Copyright 2007-2016 The OpenMx Project

  This is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "omxExpectation.h"
#include "omxFitFunction.h"
#include "omxBLAS.h"
#include "omxRAMExpectation.h"
#include "RAMInternal.h"

static void omxCalculateRAMCovarianceAndMeans(omxMatrix* A, omxMatrix* S, omxMatrix* F, 
    omxMatrix* M, omxMatrix* Cov, omxMatrix* Means, int numIters, omxMatrix* I, 
    omxMatrix* Z, omxMatrix* Y, omxMatrix* X, omxMatrix* Ax);
static omxMatrix* omxGetRAMExpectationComponent(omxExpectation* ox, const char* component);

void omxRAMExpectation::ensureTrivialF() // move to R side? TODO
{
	omxRecompute(F, NULL);  // should not do anything
	EigenMatrixAdaptor eF(F);
	Eigen::MatrixXd ident(F->rows, F->rows);
	ident.setIdentity();
	if (ident != eF.block(0, 0, F->rows, F->rows)) {
		Rf_error("Square part of F matrix is not trivial");
	}
}

static void omxCallRAMExpectation(omxExpectation* oo, const char *what, const char *how)
{
	if (what || how) {
		mxLog("RAM expectation(%s,%s)", what?what:"NULL", how?how:"NULL");
		return;
	}

    if(OMX_DEBUG) { mxLog("RAM Expectation calculating."); }
	omxRAMExpectation* oro = (omxRAMExpectation*)(oo->argStruct);
	
	omxRecompute(oro->A, NULL);
	omxRecompute(oro->S, NULL);
	omxRecompute(oro->F, NULL);
	if(oro->M != NULL)
	    omxRecompute(oro->M, NULL);
	    
	omxCalculateRAMCovarianceAndMeans(oro->A, oro->S, oro->F, oro->M, oro->cov, 
		oro->means, oro->numIters, oro->I, oro->Z, oro->Y, oro->X, oro->Ax);
}

static void omxDestroyRAMExpectation(omxExpectation* oo) {

	if(OMX_DEBUG) { mxLog("Destroying RAM Expectation."); }
	
	omxRAMExpectation* argStruct = (omxRAMExpectation*)(oo->argStruct);

	if (argStruct->rram) delete argStruct->rram;

	omxFreeMatrix(argStruct->cov);

	if(argStruct->means != NULL) {
		omxFreeMatrix(argStruct->means);
	}

	omxFreeMatrix(argStruct->I);
	omxFreeMatrix(argStruct->X);
	omxFreeMatrix(argStruct->Y);
	omxFreeMatrix(argStruct->Z);
	omxFreeMatrix(argStruct->Ax);
	delete argStruct;
}

static void refreshUnfilteredCov(omxExpectation *oo)
{
	// Ax = ZSZ' = Covariance matrix including latent variables
	omxRAMExpectation* oro = (omxRAMExpectation*) (oo->argStruct);
	omxMatrix* A = oro->A;
	omxMatrix* S = oro->S;
	omxMatrix* Ax= oro->Ax;
	omxMatrix* Z = oro->Z;
	omxMatrix* I = oro->I;
    int numIters = oro->numIters;
    
    omxRecompute(A, NULL);
    omxRecompute(S, NULL);
	
    omxShallowInverse(NULL, numIters, A, Z, Ax, I ); // Z = (I-A)^-1

    EigenMatrixAdaptor eZ(Z);
    EigenMatrixAdaptor eS(S);
    EigenMatrixAdaptor eAx(Ax);

    eAx.block(0, 0, eAx.rows(), eAx.cols()) = eZ * eS * eZ.transpose();
}

static void omxPopulateRAMAttributes(omxExpectation *oo, SEXP algebra) {
    if(OMX_DEBUG) { mxLog("Populating RAM Attributes."); }

    refreshUnfilteredCov(oo);
	omxRAMExpectation* oro = (omxRAMExpectation*) (oo->argStruct);
	omxMatrix* Ax= oro->Ax;
	
	{
	SEXP expCovExt;
	ScopedProtect p1(expCovExt, Rf_allocMatrix(REALSXP, Ax->rows, Ax->cols));
	for(int row = 0; row < Ax->rows; row++)
		for(int col = 0; col < Ax->cols; col++)
			REAL(expCovExt)[col * Ax->rows + row] =
				omxMatrixElement(Ax, row, col);
	Rf_setAttrib(algebra, Rf_install("UnfilteredExpCov"), expCovExt);
	}
	Rf_setAttrib(algebra, Rf_install("numStats"), Rf_ScalarReal(omxDataDF(oo->data)));
}

/*
 * omxCalculateRAMCovarianceAndMeans
 * 			Just like it says on the tin.  Calculates the mean and covariance matrices
 * for a RAM model.  M is the number of total variables, latent and manifest. N is
 * the number of manifest variables.
 *
 * params:
 * omxMatrix *A, *S, *F 	: matrices as specified in the RAM model.  MxM, MxM, and NxM
 * omxMatrix *M				: vector containing model implied means. 1xM
 * omxMatrix *Cov			: On output: model-implied manifest covariance.  NxN.
 * omxMatrix *Means			: On output: model-implied manifest means.  1xN.
 * int numIterations		: Precomputed number of iterations of taylor series expansion.
 * omxMatrix *I				: Identity matrix.  If left NULL, will be populated.  MxM.
 * omxMatrix *Z				: On output: Computed (I-A)^-1. MxM.
 * omxMatrix *Y, *X, *Ax	: Space for computation. NxM, NxM, MxM.  On exit, populated.
 */

static void omxCalculateRAMCovarianceAndMeans(omxMatrix* A, omxMatrix* S, omxMatrix* F, 
	omxMatrix* M, omxMatrix* Cov, omxMatrix* Means, int numIters, omxMatrix* I, 
	omxMatrix* Z, omxMatrix* Y, omxMatrix* X, omxMatrix* Ax) {
	
	if (F->rows == 0) return;

	if(OMX_DEBUG) { mxLog("Running RAM computation with numIters is %d\n.", numIters); }
		
	if(Ax == NULL || I == NULL || Z == NULL || Y == NULL || X == NULL) {
		Rf_error("Internal Error: RAM Metadata improperly populated.  Please report this to the OpenMx development team.");
	}
		
	if(Cov == NULL && Means == NULL) {
		return; // We're not populating anything, so why bother running the calculation?
	}
	
	omxShallowInverse(NULL, numIters, A, Z, Ax, I );
	
	/* Cov = FZSZ'F' */
	omxDGEMM(FALSE, FALSE, 1.0, F, Z, 0.0, Y);

	omxDGEMM(FALSE, FALSE, 1.0, Y, S, 0.0, X);

	omxDGEMM(FALSE, TRUE, 1.0, X, Y, 0.0, Cov);
	 // Cov = FZSZ'F' (Because (FZ)' = Z'F')
	
	if(OMX_DEBUG_ALGEBRA) {omxPrintMatrix(Cov, "....RAM: Model-implied Covariance Matrix:");}
	
	if(M != NULL && Means != NULL) {
		// F77_CALL(omxunsafedgemv)(Y->majority, &(Y->rows), &(Y->cols), &oned, Y->data, &(Y->leading), M->data, &onei, &zerod, Means->data, &onei);
		omxDGEMV(FALSE, 1.0, Y, M, 0.0, Means);
		if(OMX_DEBUG_ALGEBRA) {omxPrintMatrix(Means, "....RAM: Model-implied Means Vector:");}
	}
}

void omxInitRAMExpectation(omxExpectation* oo) {
	
	omxState* currentState = oo->currentState;	
	SEXP rObj = oo->rObj;

	if(OMX_DEBUG) { mxLog("Initializing RAM expectation."); }
	
	int l, k;

	SEXP slotValue;
	
	omxRAMExpectation *RAMexp = new omxRAMExpectation;
	RAMexp->rram = 0;
	
	/* Set Expectation Calls and Structures */
	oo->computeFun = omxCallRAMExpectation;
	oo->destructFun = omxDestroyRAMExpectation;
	oo->componentFun = omxGetRAMExpectationComponent;
	oo->populateAttrFun = omxPopulateRAMAttributes;
	oo->argStruct = (void*) RAMexp;
	
	/* Set up expectation structures */
	if(OMX_DEBUG) { mxLog("Initializing RAM expectation."); }

	if(OMX_DEBUG) { mxLog("Processing M."); }
	RAMexp->M = omxNewMatrixFromSlot(rObj, currentState, "M");

	if(OMX_DEBUG) { mxLog("Processing A."); }
	RAMexp->A = omxNewMatrixFromSlot(rObj, currentState, "A");

	if(OMX_DEBUG) { mxLog("Processing S."); }
	RAMexp->S = omxNewMatrixFromSlot(rObj, currentState, "S");

	if(OMX_DEBUG) { mxLog("Processing F."); }
	RAMexp->F = omxNewMatrixFromSlot(rObj, currentState, "F");

	/* Identity Matrix, Size Of A */
	if(OMX_DEBUG) { mxLog("Generating I."); }
	RAMexp->I = omxNewIdentityMatrix(RAMexp->A->rows, currentState);

	if(OMX_DEBUG) { mxLog("Processing expansion iteration depth."); }
	{ScopedProtect p1(slotValue, R_do_slot(rObj, Rf_install("depth")));
	RAMexp->numIters = INTEGER(slotValue)[0];
	if(OMX_DEBUG) { mxLog("Using %d iterations.", RAMexp->numIters); }
	}

	{
		ScopedProtect p1(slotValue, R_do_slot(rObj, Rf_install("join")));
		if (Rf_length(slotValue) && !oo->data) Rf_error("%s: data is required for joins", oo->name);
		RAMexp->joins.reserve(Rf_length(slotValue));
		for (int jx=0; jx < Rf_length(slotValue); ++jx) {
			SEXP rjoin = VECTOR_ELT(slotValue, jx);
			SEXP rfk; ScopedProtect p1(rfk, R_do_slot(rjoin, Rf_install("foreignKey")));
			SEXP rex; ScopedProtect p2(rex, R_do_slot(rjoin, Rf_install("expectation")));
			join j1;
			j1.foreignKey = Rf_asInteger(rfk) - 1;
			j1.ex = omxExpectationFromIndex(Rf_asInteger(rex), currentState);
			omxCompleteExpectation(j1.ex);
			if (!strEQ(j1.ex->expType, "MxExpectationRAM")) {
				Rf_error("%s: only MxExpectationRAM can be joined with MxExpectationRAM", oo->name);
			}
			if (omxDataIsSorted(j1.ex->data)) {
				Rf_error("%s join with %s but observed data is sorted",
					 oo->name, j1.ex->name);
			}
			if (!omxDataColumnIsKey(oo->data, j1.foreignKey)) {
				Rf_error("Cannot join using non-integer type column '%s' in '%s'. "
					 "Did you forget to use mxData(..., sort=FALSE)?",
					 omxDataColumnName(oo->data, j1.foreignKey),
					 oo->data->name);
			}
			j1.regression = omxNewMatrixFromSlot(rjoin, currentState, "regression");
			if (OMX_DEBUG) {
				mxLog("%s: join col %d against %s using regression matrix %s",
				      oo->name, j1.foreignKey, j1.ex->name, j1.regression->name());
			}
				
			RAMexp->joins.push_back(j1);
		}
	}

	l = RAMexp->F->rows;
	k = RAMexp->A->cols;

	if (k != RAMexp->S->cols || k != RAMexp->S->rows || k != RAMexp->A->rows) {
		Rf_error("RAM matrices '%s' and '%s' must have the same dimensions",
			 RAMexp->S->name(), RAMexp->A->name());
	}

	if(OMX_DEBUG) { mxLog("Generating internals for computation."); }

	RAMexp->Z = 	omxInitMatrix(k, k, TRUE, currentState);
	RAMexp->Ax = 	omxInitMatrix(k, k, TRUE, currentState);
	RAMexp->Ax->rownames = RAMexp->S->rownames;
	RAMexp->Ax->colnames = RAMexp->S->colnames;
	RAMexp->Y = 	omxInitMatrix(l, k, TRUE, currentState);
	RAMexp->X = 	omxInitMatrix(l, k, TRUE, currentState);
	
	RAMexp->cov = 		omxInitMatrix(l, l, TRUE, currentState);

	if(RAMexp->M != NULL) {
		RAMexp->means = 	omxInitMatrix(1, l, TRUE, currentState);
	} else {
	    RAMexp->means  = 	NULL;
    }

	if (RAMexp->joins.size()) {
		RAMexp->rram = new RelationalRAMExpectation::state;
		RAMexp->rram->init(oo);
	}
}

static omxMatrix* omxGetRAMExpectationComponent(omxExpectation* ox, const char* component) {
	
	if(OMX_DEBUG) { mxLog("RAM expectation: %s requested--", component); }

	omxRAMExpectation* ore = (omxRAMExpectation*)(ox->argStruct);
	omxMatrix* retval = NULL;

	if(strEQ("cov", component)) {
		retval = ore->cov;
	} else if(strEQ("means", component)) {
		retval = ore->means;
	} else if(strEQ("pvec", component)) {
		// Once implemented, change compute function and return pvec
	}
	
	return retval;
}

namespace RelationalRAMExpectation {

	// verify whether sparse can deal with parameters set to exactly zero TODO

	void state::loadOneRow(omxExpectation *expectation, FitContext *fc, int key_or_row, int &lx)
	{
		const double signA = Global->RAMInverseOpt ? 1.0 : -1.0;
		omxData *data = expectation->data;

		int row;
		if (!data->hasPrimaryKey()) {
			row = key_or_row;
		} else {
			row = data->lookupRowOfKey(key_or_row);
			if (data->rowToOffsetMap[row] != lx) return;
		}

		data->handleDefinitionVarList(expectation->currentState, row);
		omxRAMExpectation *ram = (omxRAMExpectation*) expectation->argStruct;
		omxRecompute(ram->A, fc);
		omxRecompute(ram->S, fc);
		if (ram->M) omxRecompute(ram->M, fc);

		for (size_t jx=0; jx < ram->joins.size(); ++jx) {
			join &j1 = ram->joins[jx];
			int key = omxKeyDataElement(data, row, j1.foreignKey);
			if (key == NA_INTEGER) continue;
			loadOneRow(j1.ex, fc, key, lx);
		}
		for (size_t jx=0; jx < ram->joins.size(); ++jx) {
			join &j1 = ram->joins[jx];
			int key = omxKeyDataElement(data, row, j1.foreignKey);
			if (key == NA_INTEGER) continue;
			int frow = j1.ex->data->lookupRowOfKey(key);
			int jOffset = j1.ex->data->rowToOffsetMap[frow];
			omxMatrix *betA = j1.regression;
			omxRecompute(betA, fc);
			omxRAMExpectation *ram2 = (omxRAMExpectation*) j1.ex->argStruct;
			for (int rx=0; rx < ram->A->rows; ++rx) {  //lower
				for (int cx=0; cx < ram2->A->rows; ++cx) {  //upper
					double val = omxMatrixElement(betA, rx, cx);
					if (val == 0.0) continue;
					fullA.coeffRef(jOffset + cx, lx + rx) = signA * val;
				}
			}
		}

		if (!haveFilteredAmat) {
			EigenMatrixAdaptor eA(ram->A);
			for (int cx=0; cx < eA.cols(); ++cx) {
				for (int rx=0; rx < eA.rows(); ++rx) {
					if (rx != cx && eA(rx,cx) != 0) {
						// can't use eA.block(..) -= because fullA must remain sparse
						fullA.coeffRef(lx + cx, lx + rx) = signA * eA(rx, cx);
					}
				}
			}
		}

		EigenMatrixAdaptor eS(ram->S);
		for (int cx=0; cx < eS.cols(); ++cx) {
			for (int rx=0; rx < eS.rows(); ++rx) {
				if (rx >= cx && eS(rx,cx) != 0) {
					fullS.coeffRef(lx + rx, lx + cx) = eS(rx, cx);
				}
			}
		}

		if (ram->M) {
			EigenVectorAdaptor eM(ram->M);
			for (int mx=0; mx < eM.size(); ++mx) {
				fullMeans[lx + mx] = eM[mx];
			}
		} else {
			fullMeans.segment(lx, ram->A->cols).setZero();
		}

		lx += ram->A->cols;
	}

	void state::placeOneRow(omxExpectation *expectation, int frow, int &totalObserved, int &maxSize)
	{
		omxData *data = expectation->data;
		omxRAMExpectation *ram = (omxRAMExpectation*) expectation->argStruct;

		for (size_t jx=0; jx < ram->joins.size(); ++jx) {
			join &j1 = ram->joins[jx];
			int key = omxKeyDataElement(data, frow, j1.foreignKey);
			if (key == NA_INTEGER) continue;
			placeOneRow(j1.ex, j1.ex->data->lookupRowOfKey(key), totalObserved, maxSize);
		}
		if (data->hasPrimaryKey()) {
			if (data->rowToOffsetMap.size() == 0) {
				ram->ensureTrivialF();
			}

			// insert_or_assign would be nice here
			std::map<int,int>::const_iterator it = data->rowToOffsetMap.find(frow);
			if (it != data->rowToOffsetMap.end()) return;

			if (verbose >= 2) {
				mxLog("%s: place row %d at %d", expectation->name, frow, maxSize);
			}
			data->rowToOffsetMap[frow] = maxSize;
		}
		int jCols = expectation->dataColumns->cols;
		if (jCols) {
			if (!ram->M) {
				Rf_error("'%s' has manifest observations but '%s' has no mean model",
					 data->name, expectation->name);
			}
			if (smallCol->cols < jCols) {
				omxResizeMatrix(smallCol, 1, jCols);
			}
			omxDataRow(expectation, frow, smallCol);
			for (int col=0; col < jCols; ++col) {
				double val = omxMatrixElement(smallCol, 0, col);
				bool yes = std::isfinite(val);
				if (yes) ++totalObserved;
			}
		}
		maxSize += ram->F->cols;
		AmatDependsOnParameters |= ram->A->dependsOnParameters();
	}

	void state::prepOneRow(omxExpectation *expectation, int row_or_key, int &lx, int &dx)
	{
		omxData *data = expectation->data;
		omxRAMExpectation *ram = (omxRAMExpectation*) expectation->argStruct;

		int row;
		if (!data->hasPrimaryKey()) {
			row = row_or_key;
		} else {
			row = data->lookupRowOfKey(row_or_key);
			if (data->rowToOffsetMap[row] != lx) return;
		}

		if (Global->RAMInverseOpt) {
			data->handleDefinitionVarList(expectation->currentState, row);
			omxRAMExpectation *ram = (omxRAMExpectation*) expectation->argStruct;
			omxRecompute(ram->A, NULL);
		}

		for (size_t jx=0; jx < ram->joins.size(); ++jx) {
			join &j1 = ram->joins[jx];
			int key = omxKeyDataElement(data, row, j1.foreignKey);
			if (key == NA_INTEGER) continue;
			prepOneRow(j1.ex, key, lx, dx);
		}

		if (Global->RAMInverseOpt) {
			for (size_t jx=0; jx < ram->joins.size(); ++jx) {
				join &j1 = ram->joins[jx];
				int key = omxKeyDataElement(data, row, j1.foreignKey);
				if (key == NA_INTEGER) continue;
				int frow = j1.ex->data->lookupRowOfKey(key);
				int jOffset = j1.ex->data->rowToOffsetMap[frow];
				omxMatrix *betA = j1.regression;
				omxRecompute(betA, NULL);
				betA->markPopulatedEntries();
				omxRAMExpectation *ram2 = (omxRAMExpectation*) j1.ex->argStruct;
				for (int rx=0; rx < ram->A->rows; ++rx) {  //lower
					for (int cx=0; cx < ram2->A->rows; ++cx) {  //upper
						double val = omxMatrixElement(betA, rx, cx);
						if (val == 0.0) continue;
						depthTestA.coeffRef(lx + rx, jOffset + cx) = 1;
					}
				}
			}
			ram->A->markPopulatedEntries();
			EigenMatrixAdaptor eA(ram->A);
			for (int cx=0; cx < eA.cols(); ++cx) {
				for (int rx=0; rx < eA.rows(); ++rx) {
					if (rx != cx && eA(rx,cx) != 0) {
						depthTestA.coeffRef(lx + rx, lx + cx) = 1;
					}
				}
			}
		}

		int jCols = expectation->dataColumns->cols;
		if (jCols) {
			omxDataRow(expectation, row, smallCol);
			for (int col=0; col < jCols; ++col) {
				double val = omxMatrixElement(smallCol, 0, col);
				bool yes = std::isfinite(val);
				if (!yes) continue;
				latentFilter[ lx + col ] = true;
				dataVec[ dx++ ] = val;
			}
		}
		nameVec.insert(nameVec.end(), ram->A->colnames.begin(), ram->A->colnames.end());
		lx += ram->F->cols;
	}

	void state::init(omxExpectation *expectation)
	{
		homeEx = expectation;
		{
			SEXP tmp;
			ScopedProtect p1(tmp, R_do_slot(homeEx->rObj, Rf_install("verbose")));
			verbose = Rf_asInteger(tmp) + OMX_DEBUG;
		}


		omxRAMExpectation *ram = (omxRAMExpectation*) homeEx->argStruct;
		ram->ensureTrivialF();

		int numManifest = ram->F->rows;

		AmatDependsOnParameters = ram->A->dependsOnParameters();
		haveFilteredAmat = false;
		smallCol = omxInitMatrix(1, numManifest, TRUE, homeEx->currentState);
		omxData *data               = homeEx->data;

		// don't permit reuse of our expectations by some other fit function TODO

		int totalObserved = 0;
		int maxSize = 0;
		for (int row=0; row < data->rows; ++row) {
			placeOneRow(homeEx, row, totalObserved, maxSize);
		}
		//mxLog("AmatDependsOnParameters=%d", AmatDependsOnParameters);

		if (verbose >= 1) {
			mxLog("%s: total observations %d", homeEx->name, totalObserved);
		}
		nameVec.reserve(maxSize);
		latentFilter.assign(maxSize, false); // will have totalObserved true entries
		dataVec.resize(totalObserved);
		depthTestA.resize(maxSize, maxSize);

		FreeVarGroup *varGroup = Global->findVarGroup(FREEVARGROUP_ALL); // ignore freeSet

		if (Global->RAMInverseOpt) {
			Eigen::VectorXd vec(varGroup->vars.size());
			vec.setConstant(1);
			copyParamToModelInternal(varGroup, homeEx->currentState, vec.data());
		}

		for (int row=0, dx=0, lx=0; row < data->rows; ++row) {
			int key_or_row = data->hasPrimaryKey()? data->primaryKeyOfRow(row) : row;
			prepOneRow(homeEx, key_or_row, lx, dx);
		}

		AshallowDepth = -1;

		if (Global->RAMInverseOpt) {
			int maxDepth = std::min(maxSize, 30);
			if (Global->RAMMaxDepth != NA_INTEGER) maxDepth = Global->RAMMaxDepth;
			Eigen::SparseMatrix<double> curProd = depthTestA;
			for (int tx=1; tx < maxDepth; ++tx) {
				if (verbose >= 3) { Eigen::MatrixXd tmp = curProd; mxPrintMat("curProd", tmp); }
				curProd = (curProd * depthTestA).eval();
				bool allZero = true;
				for (int k=0; k < curProd.outerSize(); ++k) {
					for (Eigen::SparseMatrix<double>::InnerIterator it(curProd, k); it; ++it) {
						if (it.value() != 0.0) {
							allZero = false;
							break;
						}
					}
				}
				if (allZero) {
					AshallowDepth = tx;
					break;
				}
			}
			homeEx->currentState->setDirty();
		}
		if (verbose >= 1) {
			mxLog("%s: RAM shallow inverse depth = %d", homeEx->name, AshallowDepth);
		}
	}

	state::~state()
	{
		omxFreeMatrix(smallCol);
	}

	void state::compute(FitContext *fc)
	{
		omxData *data                           = homeEx->data;

		fullMeans.conservativeResize(latentFilter.size());

		if (fullA.nonZeros() == 0) {
			analyzedFullA = false;
			fullA.resize(latentFilter.size(), latentFilter.size());
		}
		fullS.conservativeResize(latentFilter.size(), latentFilter.size());

		if (ident.nonZeros() == 0) {
			ident.resize(fullA.rows(), fullA.rows());
			ident.setIdentity();
		}

		for (int lx=0, row=0; row < data->rows; ++row) {
			int key_or_row = data->hasPrimaryKey()? data->primaryKeyOfRow(row) : row;
			loadOneRow(homeEx, fc, key_or_row, lx);
		}

		//{ Eigen::MatrixXd tmp = fullA; mxPrintMat("fullA", tmp); }
		//{ Eigen::MatrixXd tmp = fullS; mxPrintMat("fullS", tmp); }

		if (!haveFilteredAmat) {
			// consider http://users.clas.ufl.edu/hager/papers/Lightning/update.pdf ?
			if (AshallowDepth >= 0) {
				fullA.makeCompressed();
				filteredA = fullA + ident;
				for (int iter=1; iter <= AshallowDepth; ++iter) {
					filteredA = (filteredA * fullA + ident).eval();
					//{ Eigen::MatrixXd tmp = filteredA; mxPrintMat("filteredA", tmp); }
				}
			} else {
				fullA += ident;
				if (!analyzedFullA) {
					analyzedFullA = true;
					fullA.makeCompressed();
					Asolver.analyzePattern(fullA);
				}
				Asolver.factorize(fullA);

				filteredA = Asolver.solve(ident);
				//{ Eigen::MatrixXd tmp = filteredA; mxPrintMat("filteredA", tmp); }
			}

			// We built A transposed so we can quickly filter columns
			// Switch to filterOuter http://eigen.tuxfamily.org/bz/show_bug.cgi?id=1130
			filteredA.uncompress();
			Eigen::SparseMatrix<double>::Index *op = filteredA.outerIndexPtr();
			Eigen::SparseMatrix<double>::Index *nzp = filteredA.innerNonZeroPtr();
			int dx = 0;
			for (int cx=0; cx < fullA.cols(); ++cx) {
				if (!latentFilter[cx]) continue;
				op[dx] = op[cx];
				nzp[dx] = nzp[cx];
				++dx;
			}
			op[dx] = op[fullA.cols()];
			filteredA.conservativeResize(fullA.rows(), dataVec.size());

			//{ Eigen::MatrixXd tmp = filteredA; mxPrintMat("filteredA", tmp); }
			haveFilteredAmat = !AmatDependsOnParameters;
		}
		//mxPrintMat("S", fullS);
		//Eigen::MatrixXd fullCovDense =

		fullCov = (filteredA.transpose() * fullS.selfadjointView<Eigen::Lower>() * filteredA);
		//{ Eigen::MatrixXd tmp = fullCov; mxPrintMat("fullcov", tmp); }
	}

};
