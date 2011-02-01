/*
 *  Copyright 2007-2010 The OpenMx Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/***********************************************************
*
*  omxAlgebraFunctions.c
*
*  Created: Timothy R. Brick 	Date: 2008-11-13 12:33:06
*
*	Includes the functions required for omxAlgebra statements.
*   These functions should take a number of values that
*   evenly matches the number of args requested by the
*   omxSymbolTable.
*
**********************************************************/

#include "omxAlgebraFunctions.h"
#include "omxMatrix.h"
#include "merge.h"
#include "omxBLAS.h"

/* Helper Functions */
extern void F77_SUB(sadmvn)(int*, double*, double*, int*, double*, int*, double*, double*, double*, double*, int*);

void omxStandardizeCovMatrix(omxMatrix* cov, double* corList, double* weights) {
	// Maybe coerce this into an algebra or sequence of algebras?

	if(OMX_DEBUG) { Rprintf("Standardizing matrix."); }

	int rows = cov->rows;

	for(int i = 0; i < rows; i++) {
		weights[i] = sqrt(omxMatrixElement(cov, i, i));
	}

	for(int i = 0; i < rows; i++) {
		for(int j = 0; j < i; j++) {
			corList[((i*(i-1))/2) + j] = omxMatrixElement(cov, i, j) / (weights[i] * weights[j]);
		}
	}
}

void checkIncreasing(omxMatrix* om, int column) {
	double previous = - INFINITY;
	double current;
	for(int j = 0; j < om->rows; j++ ) {
		current = omxMatrixElement(om, j, column);
		if(current == NA_REAL || current == NA_INTEGER) {
			continue;
		}
		if(current <= previous) {
			char *errstr = Calloc(250, char);
			sprintf(errstr, "Thresholds are not strictly increasing.");
			omxRaiseError(om->currentState, -1, errstr);
			Free(errstr);
		}
	}
}



// TODO: Implement wrappers for BLAS functions used here.

/* omxAlgebraFunction Wrappers */

void omxMatrixTranspose(omxMatrix** matList, int numArgs, omxMatrix* result) {

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Transpose.\n");}

	omxMatrix* inMat = matList[0];

	omxCopyMatrix(result, inMat);
	result->colMajor = !result->colMajor;
	int rowtemp = result->rows;
	result->rows = result->cols;
	result->cols = rowtemp;
	omxMatrixCompute(result);
}

void omxMatrixInvert(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Invert.\n");}

	omxMatrix* inMat = matList[0];

	int ipiv[inMat->rows], lwork;
	lwork = 4 * inMat->rows * inMat->cols;
	double work[lwork];
	int l = 0;

	omxCopyMatrix(result, inMat);
	F77_CALL(dgetrf)(&(result->cols), &(result->rows), result->data, &(result->leading), ipiv, &l);
	if(l != 0) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Attempted to invert non-invertable matrix.");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}
	F77_CALL(dgetri)(&(result->cols), result->data, &(result->leading), ipiv, work, &lwork, &l);

}

void omxElementPower(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Element Powering.\n");}

	omxMatrix* inMat = matList[0];
	omxMatrix* power = matList[1];

	int rows = inMat->rows;
	int cols = inMat->cols;

	if(result->cols != cols || result->rows != rows)
		omxResizeMatrix(result, rows, cols, FALSE);

	if(power->rows == rows && power->cols == cols) {
		for(int j = 0; j < inMat->rows; j++)
			for(int k = 0; k < inMat->cols; k++)
				omxSetMatrixElement(result, j, k, pow(omxMatrixElement(inMat, j,k), omxMatrixElement(power, j,k)));
	} else {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Non-conformable matrices in ^ operator.");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

}

void omxMatrixMult(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Multiply.\n");}

	omxMatrix* preMul = matList[0];
	omxMatrix* postMul = matList[1];

	if(preMul == NULL || postMul == NULL) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Null matrix pointer detected.\n");
		Free(errstr);
	}

	static double zero = 0.0;
	static double one = 1.0;

	/* Conformability Check! */
	if(preMul->cols != postMul->rows) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Non-conformable matrices [(%d x %d) and (%d x %d)] in Matrix Multiply.", preMul->rows, preMul->cols, postMul->rows, postMul->cols);
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	if(result->rows != preMul->rows || result->cols != postMul->cols)
		omxResizeMatrix(result, preMul->rows, postMul->cols, FALSE);

	/* For debugging */
	if(OMX_DEBUG_ALGEBRA) {
		omxPrint(result, "NewMatrix");
		Rprintf("DGEMM: %c, %c, %d, %d, %d, %f, %0x %d %0x %d %f %0x %d\n", *(preMul->majority), *(postMul->majority), (preMul->rows), (postMul->cols), (preMul->cols), one, (preMul->data), (preMul->leading), (postMul->data), (postMul->leading), zero, (result->data), (result->leading));
	}

	/* The call itself */
	F77_CALL(omxunsafedgemm)((preMul->majority), (postMul->majority), &(preMul->rows), &(postMul->cols), &(preMul->cols), &one, preMul->data, &(preMul->leading), postMul->data, &(postMul->leading), &zero, result->data, &(result->leading));
	result->colMajor = TRUE;
	omxMatrixCompute(result);

}

void omxMatrixElementMult(omxMatrix** matList, int numArgs, omxMatrix* result)
{
	omxMatrix* first = matList[0];
	omxMatrix* second = matList[1];

	/* Conformability Check! */
	if(first->cols != second->cols || first->rows != second->rows) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Non-conformable matrices in Element Multiplication.");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	omxCopyMatrix(result, first);

	int rows = first->rows;
	int cols = first->cols;

	for(int i = 0; i < rows; i++) {
		for(int j = 0; j < cols; j++) {
			omxSetMatrixElement(result, i, j,
				omxMatrixElement(result, i, j) *
				omxMatrixElement(second, i, j));
		}
	}
}


void omxKroneckerProd(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Kronecker Product.\n");}

	omxMatrix* preMul = matList[0];
	omxMatrix* postMul = matList[1];

	int rows = preMul->rows * postMul->rows;
	int cols = preMul->cols * postMul->cols;

	if(result->rows != rows || result->cols != cols)
		omxResizeMatrix(result, rows, cols, FALSE);

	for(int preRow = 0; preRow < preMul->rows; preRow++)
		for(int postRow = 0; postRow < postMul->rows; postRow++)
			for(int preCol = 0; preCol < preMul->cols; preCol++)
				for(int postCol = 0; postCol < postMul->cols; postCol++)
					omxSetMatrixElement(result, preRow*postMul->rows + postRow,
						preCol*postMul->cols + postCol,
						omxMatrixElement(preMul, preRow, preCol) * omxMatrixElement(postMul, postRow, postCol));
}

void omxKroneckerPower(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Kronecker Power.\n");}

	omxMatrix* preMul = matList[0];
	omxMatrix* postMul = matList[1];

	int rows = preMul->rows * postMul->rows;
	int cols = preMul->cols * postMul->cols;

	if(result->rows != rows || result->cols != cols)
		omxResizeMatrix(result, rows, cols, FALSE);

	for(int preRow = 0; preRow < preMul->rows; preRow++)
		for(int postRow = 0; postRow < postMul->rows; postRow++)
			for(int preCol = 0; preCol < preMul->cols; preCol++)
				for(int postCol = 0; postCol < postMul->cols; postCol++)
					omxSetMatrixElement(result, preRow*postMul->rows + postRow,
						preCol*postMul->cols + postCol,
						pow(omxMatrixElement(preMul, preRow, preCol), omxMatrixElement(postMul, postRow, postCol)));
}

void omxQuadraticProd(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Quadratic Product.\n");}

	omxMatrix* preMul = matList[0];
	omxMatrix* postMul = matList[1];
	/* A %&% B = ABA' */

	static double zero = 0.0;
	static double one = 1.0;

	/* Conformability Check! */
	if(preMul->cols != postMul->rows || postMul->rows != postMul->cols)
		omxRaiseError(preMul->currentState, -1, "Non-conformable matrices in Matrix Quadratic Product.");

	omxMatrix* intermediate = NULL;
	intermediate = omxInitTemporaryMatrix(NULL, preMul->rows, postMul->cols, TRUE, preMul->currentState);

	if(OMX_DEBUG_ALGEBRA) { Rprintf("Quadratic: os = 0x%x, step = %d.\n", result->currentState, intermediate->currentState->computeCount);}

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Quadratic Product: Readying result matrix.(%x, %x)\n", result->algebra, result->objective);}

	if(result->rows != preMul->rows || result->cols != preMul->rows)
		omxResizeMatrix(result, preMul->rows, preMul->rows, FALSE);

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Quadratic Product: Readying intermediate Matrix.(%x, %x)\n", intermediate->algebra, intermediate->objective);}

	omxMatrixCompute(intermediate);
	omxMatrixCompute(result);

	/* The call itself */
	if(OMX_DEBUG_ALGEBRA) { Rprintf("Quadratic: premul.\n");}
	F77_CALL(omxunsafedgemm)((preMul->majority), (postMul->majority), &(preMul->rows), &(postMul->cols), &(preMul->cols), &one, preMul->data, &(preMul->leading), postMul->data, &(postMul->leading), &zero, intermediate->data, &(intermediate->leading));
	omxMatrixCompute(intermediate);
	if(OMX_DEBUG_ALGEBRA) { Rprintf("Quadratic: postmul.\n");}
//	if(OMX_DEBUG_ALGEBRA) { Rprintf("Quadratic postmul: result is (%d x %d), %d leading, inter is (%d x %d), prem is (%d x %d), post is (%d x %d).\n", result->rows, result->cols, result->leading, intermediate->rows, intermediate->cols, preMul->rows, preMul->cols, postMul->rows, postMul->cols);}
	F77_CALL(omxunsafedgemm)((intermediate->majority), (preMul->minority), &(intermediate->rows), &(preMul->rows), &(intermediate->cols), &one, intermediate->data, &(intermediate->leading), preMul->data, &(preMul->leading), &zero, result->data, &(result->leading));
	if(OMX_DEBUG_ALGEBRA) { Rprintf("Quadratic: clear.\n");}

	omxFreeAllMatrixData(intermediate);

}

void omxElementDivide(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Element Division.\n");}

	omxMatrix* inMat = matList[0];
	omxMatrix* divisor = matList[1];

	/* Conformability Check! */
	if(inMat->cols != divisor->cols || inMat->rows != divisor->rows) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Non-conformable matrices in Element Division.");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	omxCopyMatrix(result, inMat);

	int rows = inMat->rows;
	int cols = inMat->cols;

	for(int i = 0; i < rows; i++) {
		for(int j = 0; j < cols; j++) {
			omxSetMatrixElement(result, i, j,
				omxMatrixElement(result, i, j) /
				omxMatrixElement(divisor, i, j));
		}
	}
}

void omxUnaryNegation(omxMatrix** matList, int numArgs, omxMatrix* result)
{
	if (OMX_DEBUG_ALGEBRA) {Rprintf("ALGEBRA: Unary Negation.\n");}

	omxMatrix* inMat = matList[0];

	int rows = inMat->rows;
	int cols = inMat->cols;

	if((rows != result->rows) || (cols!= result->cols)){
		omxResizeMatrix(result, rows, cols, FALSE);
	}
	int vec_length = rows * cols;
	for (int i=0; i<vec_length; i++){
		double ith_value = omxVectorElement(inMat, i);
		if (ith_value == 0.0){
			omxSetVectorElement(result, i, 1.0);
		}
		else {
			omxSetVectorElement(result, i, 0.0);
		}
	}
}

void omxMatrixAdd(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Addition.\n");}

	omxMatrix* inMat = matList[0];
	omxMatrix* addend = matList[1];

	/* Conformability Check! */
	if(inMat->cols != addend->cols || inMat->rows != addend->rows) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Non-conformable matrices in Matrix Addition.");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	omxCopyMatrix(result, inMat);

	int rows = inMat->rows;
	int cols = inMat->cols;

	for(int i = 0; i < rows; i++) {
		for(int j = 0; j < cols; j++) {
			omxSetMatrixElement(result, i, j,
				omxMatrixElement(result, i, j) +
				omxMatrixElement(addend, i, j));
		}
	}
}

int matrixExtractIndices(omxMatrix *source, int dimLength, int **indices, omxMatrix *result) {

	int *retval;
	/* Case 1: the source vector contains no elements */
	if (source->rows == 0 || source->cols == 0) {
		retval = (int*) Calloc(dimLength, int);
		for(int i = 0; i < dimLength; i++) {
			retval[i] = i;
		}
		*indices = retval;
		return(dimLength);
	}
	int zero = 0, positive = 0, negative = 0;
	/* Count the number of zero, positive, and negative elements */
	for(int i = 0; i < source->rows * source->cols; i++) {
		double delement = omxVectorElement(source, i);
		if (!R_finite(delement)) {
			char *errstr = Calloc(250, char);
			sprintf(errstr, "non-finite value in '[' operator.\n");
			omxRaiseError(result->currentState, -1, errstr);
			Free(errstr);
			return(0);
		}
		int element = (int) delement;
		if (element < 0) {
			/* bounds checking */
			if (element < - dimLength) {
				char *errstr = Calloc(250, char);
				sprintf(errstr, "index %d is out of bounds in '[' operator.", element);
				omxRaiseError(result->currentState, -1, errstr);
				Free(errstr);
				return(0);
			}
			negative++;
		} else if (element == 0) {
			zero++;
		} else {
			/* bounds checking */
			if (element > dimLength) {
				char *errstr = Calloc(250, char);
				sprintf(errstr, "index %d is out of bounds in '[' operator.", element);
				omxRaiseError(result->currentState, -1, errstr);
				Free(errstr);
				return(0);
			}
			positive++;
		}
	}
	/* It is illegal to mix positive and negative elements */
	if (positive > 0 && negative > 0) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Positive and negative indices together in '[' operator.");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
		return(0);
	}
	/* convert negative indices into a list of positive indices */
	if (negative > 0) {
		int *track = Calloc(dimLength, int);
		int length = dimLength;
		for(int i = 0; i < source->rows * source->cols; i++) {
			int element = (int) omxVectorElement(source, i);
			if (element < 0) {
				if (!track[-element - 1]) length--;
				track[-element - 1]++;
			}
		}
		if (length == 0) {
			Free(track);
			return(0);
		}
		retval = Calloc(length, int);
		int j = 0;
		for(int i = 0; i < dimLength; i++) {
			if(!track[i]) {
				retval[j++] = i;
			}
		}
		Free(track);
		*indices = retval;
		return(length);
	}
	/* convert positive indices with offset of zero instead of one */
	if (positive > 0) {
		int length = positive - zero;
		retval = Calloc(length, int);
		int j = 0;
		for(int i = 0; i < source->rows * source->cols; i++) {
			int element = (int) omxVectorElement(source, i);
			if (element > 0) {
				retval[j++] = element - 1;
			}
		}
		*indices = retval;
		return(length);
	}
	/* return zero length if no positive or negative elements */
	return(0);
}

void omxMatrixExtract(omxMatrix** matList, int numArgs, omxMatrix* result) {

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Extract.\n");}

	omxMatrix* inMat = matList[0];
	omxMatrix* rowMatrix = matList[1];
	omxMatrix* colMatrix = matList[2];

	if(OMX_DEBUG_ALGEBRA) { omxPrint(rowMatrix, "Row matrix: "); }
	if(OMX_DEBUG_ALGEBRA) { omxPrint(colMatrix, "Col matrix: "); }

	int *rowIndices, *colIndices;
	int rowIndexLength, colIndexLength;

	rowIndexLength = matrixExtractIndices(rowMatrix, inMat->rows, &rowIndices, result);
	colIndexLength = matrixExtractIndices(colMatrix, inMat->cols, &colIndices, result);

	if (result->rows != rowIndexLength || result->cols != colIndexLength) {
		omxResizeMatrix(result, rowIndexLength, colIndexLength, FALSE);
	}

	for(int row = 0; row < rowIndexLength; row++) {
		for(int col = 0; col < colIndexLength; col++) {
			if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Extract: (%d, %d)[%d, %d] <- (%d, %d)[%d,%d].\n", result->rows, result->cols, row, col, rowIndexLength, colIndexLength, rowIndices[row], colIndices[col]);}
			double element = omxMatrixElement(inMat, rowIndices[row], colIndices[col]);
			omxSetMatrixElement(result, row, col, element);
		}
	}

	if (rowIndexLength > 0) Free(rowIndices);
	if (colIndexLength > 0) Free(colIndices);

}

void omxMatrixSubtract(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Subtraction.\n");}

	omxMatrix* inMat = matList[0];
	omxMatrix* subtrahend = matList[1];

	if(inMat->cols != subtrahend->cols || inMat->rows != subtrahend->rows) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Non-conformable matrices in Matrix Subtract.");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	omxCopyMatrix(result, inMat);

	if(OMX_DEBUG_ALGEBRA) {
		omxPrint(subtrahend, "Subtracting");
		omxPrint(inMat, "From");
	}

	int rows = inMat->rows;
	int cols = inMat->cols;

	for(int i = 0; i < rows; i++) {
		for(int j = 0; j < cols; j++) {
			omxSetMatrixElement(result, i, j,
				omxMatrixElement(result, i, j) -
				omxMatrixElement(subtrahend, i, j));
		}
	}

	if(OMX_DEBUG_ALGEBRA) {
		omxPrint(inMat, "And Got");
	}
}

void omxUnaryMinus(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Unary Minus.\n");}

	omxMatrix* inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = - data[j];
	}

}

void omxMatrixHorizCat(omxMatrix** matList, int numArgs, omxMatrix* result) {

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Horizontal Matrix Concatenation.\n");}

	int totalRows = 0, totalCols = 0, currentCol=0;

	if(numArgs == 0) return;

	totalRows = matList[0]->rows;			// Assumed constant.  Assert this below.

	for(int j = 0; j < numArgs; j++) {
		if(totalRows != matList[j]->rows) {
			char *errstr = Calloc(250, char);
			sprintf(errstr, "Non-conformable matrices in horizontal concatenation (cbind). First argument has %d rows, and argument #%d has %d rows.", totalRows, j + 1, matList[j]->rows);
			omxRaiseError(result->currentState, -1, errstr);
			Free(errstr);
			return;
		}
		totalCols += matList[j]->cols;
	}

	if(result->rows != totalRows || result->cols != totalCols) {
		if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: HorizCat: resizing result.\n");}
		omxResizeMatrix(result, totalRows, totalCols, FALSE);
	}

	int allArgumentsColMajor = result->colMajor;
	for(int j = 0; j < numArgs && allArgumentsColMajor; j++) {
		if (!matList[j]->colMajor) allArgumentsColMajor = 0;
	}

	if (allArgumentsColMajor) {
		int offset = 0;
		for(int j = 0; j < numArgs; j++) {	
			omxMatrix* current = matList[j];
			int size = current->rows * current->cols;
			memcpy(result->data + offset, current->data, size * sizeof(double));
			offset += size;
		}
	} else {
		for(int j = 0; j < numArgs; j++) {
			for(int k = 0; k < matList[j]->cols; k++) {
				for(int l = 0; l < totalRows; l++) {		// Gotta be a faster way to do this.
					omxSetMatrixElement(result, l, currentCol, omxMatrixElement(matList[j], l, k));
				}
				currentCol++;
			}
		}
	}

}

void omxMatrixVertCat(omxMatrix** matList, int numArgs, omxMatrix* result) {

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Vertical Matrix Concatenation.\n");}

	int totalRows = 0, totalCols = 0, currentRow=0;

	if(numArgs == 0) return;

	totalCols = matList[0]->cols;			// Assumed constant.  Assert this below.

	for(int j = 0; j < numArgs; j++) {
		if(totalCols != matList[j]->cols) {
			char *errstr = Calloc(250, char);
			sprintf(errstr, "Non-conformable matrices in vertical concatenation (rbind). First argument has %d cols, and argument #%d has %d cols.", totalCols, j + 1, matList[j]->cols);
			omxRaiseError(result->currentState, -1, errstr);
			Free(errstr);
			return;
		}
		totalRows += matList[j]->rows;
	}

	if(result->rows != totalRows || result->cols != totalCols) {
		omxResizeMatrix(result, totalRows, totalCols, FALSE);
	}

	int allArgumentsRowMajor = !result->colMajor;
	for(int j = 0; j < numArgs && allArgumentsRowMajor; j++) {
		if (matList[j]->colMajor) allArgumentsRowMajor = 0;
	}

	if (allArgumentsRowMajor) {
		int offset = 0;
		for(int j = 0; j < numArgs; j++) {	
			omxMatrix* current = matList[j];
			int size = current->rows * current->cols;	
			memcpy(result->data + offset, current->data, size * sizeof(double));
			offset += size;
		}
	} else {
		for(int j = 0; j < numArgs; j++) {
			for(int k = 0; k < matList[j]->rows; k++) {
				for(int l = 0; l < totalCols; l++) {		// Gotta be a faster way to do this.
					omxSetMatrixElement(result, currentRow, l, omxMatrixElement(matList[j], k, l));
				}
				currentRow++;
			}
		}
	}

}

void omxMatrixDeterminant(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Determinant.\n");}

	omxMatrix* inMat = matList[0];
	omxMatrix* calcMat;					// This should be preallocated.

	int rows = inMat->rows;
	int cols = inMat->cols;
	double det = 1;
	int info;

	if(rows != cols) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Determinant of non-square matrix cannot be found.\n");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	if(result->rows != 1 || result->cols != 1) {
		omxResizeMatrix(result, 1, 1, FALSE);
	}

	calcMat = omxInitTemporaryMatrix(NULL, rows, cols, TRUE, inMat->currentState);
	omxCopyMatrix(calcMat, inMat);

	int ipiv[rows];

	F77_CALL(dgetrf)(&(calcMat->rows), &(calcMat->cols), calcMat->data, &(calcMat->cols), ipiv, &info);

	if(info != 0) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Determinant Calculation: Nonsingular matrix (at row %d) on LUP decomposition.", info);
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	if(OMX_DEBUG_ALGEBRA) {
		omxPrint(calcMat, "LU Decomp");
		Rprintf("info is %d.\n", info);
	}

	for(int i = 0; i < rows; i++) {
		det *= omxMatrixElement(calcMat, i, i);
		if(ipiv[i] != (i+1)) det *= -1;
	}

	if(OMX_DEBUG_ALGEBRA) {
		Rprintf("det is %d.\n", det);
	}

	omxFreeAllMatrixData(calcMat);

	omxSetMatrixElement(result, 0, 0, det);
}

void omxMatrixTrace(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Trace.\n");}

	omxMatrix* inMat = matList[0];

	/* Consistency check: */
	if(result->rows != 1 || result->cols != 1) {
		omxResizeMatrix(result, 1, 1, FALSE);
	}

	if(inMat->rows != inMat->cols) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Non-square matrix in Trace().\n");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	double trace = 0.0;
	float min = 10E20;
	float max = 0.0;

	/* Note: This algorithm is numerically unstable.  Sorry, dudes. */
	for(int j = 0; j < inMat->rows; j++) {
		float thisElement = omxMatrixElement(inMat, j, j);
		if(fabs(min) > fabs(thisElement)) min = thisElement;
		if(fabs(max) < fabs(thisElement)) max = thisElement;
		trace += omxMatrixElement(inMat, j, j);
	}

	if(fabs(max/min) > 10E10) {
		// TODO: Warn or sort-and-sum if numerical instability shows up.
		// char errstr[250];
		// sprintf(errstr, "Matrix trace() may be numerically unstable.\n");
		// omxRaiseError(result->currentState, -1, errstr);
	}

	omxSetMatrixElement(result, 0, 0, trace);
};

void omxMatrixTotalSum(omxMatrix** matList, int numArgs, omxMatrix* result) {

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Total Sum.\n");}

	/* Consistency check: */
	if(result->rows != 1 || result->cols != 1) {
		omxResizeMatrix(result, 1, 1, FALSE);
	}

	double sum = 0.0;

	/* Note: This algorithm is numerically unstable.  Sorry, dudes. */
	for(int j = 0; j < numArgs; j++) {
		double* data = matList[j]->data;
		int matlength = matList[j]->rows * matList[j]->cols;
		for(int k = 0; k < matlength; k++) {
			sum += data[k];
		}
	}

	omxSetMatrixElement(result, 0, 0, sum);
}

void omxMatrixTotalProduct(omxMatrix** matList, int numArgs, omxMatrix* result) {

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Total Product.\n");}

	/* Consistency check: */
	if(result->rows != 1 || result->cols != 1) {
		omxResizeMatrix(result, 1, 1, FALSE);
	}

	double product = 1.0;

	/* Note: This algorithm is numerically unstable.  Sorry, dudes. */
	for(int j = 0; j < numArgs; j++) {
		double* data = matList[j]->data;
		int matlength = matList[j]->rows * matList[j]->cols;
		for(int k = 0; k < matlength; k++) {
			product *= data[k];
		}
	}

	omxSetMatrixElement(result, 0, 0, product);
}

void omxMatrixMinimum(omxMatrix** matList, int numArgs, omxMatrix* result){

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Minimum Element.\n");}

	/* Consistency check: */
	if(result->rows != 1 || result->cols != 1) {
		omxResizeMatrix(result, 1, 1, FALSE);
	}

	double min = DBL_MAX; // DBL_MAX is the maximum possible DOUBLE value, usually 10e37.
						  // We could change this to use NPSOL's INFINITY, but why bother?

	for(int j = 0; j < numArgs; j++) {
		double* data = matList[j]->data;
		int matlength = matList[j]->rows * matList[j]->cols;
		for(int k = 0; k < matlength; k++) {
			if(data[k] < min) min = data[k];
		}
	}

	omxSetMatrixElement(result, 0, 0, min);
}

void omxMatrixMaximum(omxMatrix** matList, int numArgs, omxMatrix* result){

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Maximum Element.\n");}

	/* Consistency check: */
	if(result->rows != 1 || result->cols != 1) {
		omxResizeMatrix(result, 1, 1, FALSE);
	}

	double max = -DBL_MAX;

	for(int j = 0; j < numArgs; j++) {
		double* data = matList[j]->data;
		int matlength = matList[j]->rows * matList[j]->cols;
		for(int k = 0; k < matlength; k++) {
			if(data[k] > max) max = data[k];
		}
	}

	omxSetMatrixElement(result, 0, 0, max);
}

void omxMatrixAbsolute(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Absolute Value.\n");}

	omxMatrix* inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = fabs(data[j]);
	}

}

void omxMatrixDiagonal(omxMatrix** matList, int numArgs, omxMatrix* result) {

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: diag2vec.\n");}

	omxMatrix* inMat = matList[0];
	int diags = inMat->cols;
	if(inMat->cols > inMat->rows) {
		diags = inMat->rows;
	}

	if (result->cols != 1 || result->rows != diags) {
		omxResizeMatrix(result, diags, 1, FALSE);
	}

	for(int j = 0; j < diags; j++) {
		omxSetMatrixElement(result, j, 0, omxMatrixElement(inMat, j, j));
	}

}

void omxMatrixFromDiagonal(omxMatrix** matList, int numArgs, omxMatrix* result) {

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: vec2diag.\n");}

	omxMatrix* inMat = matList[0];
	int diags = inMat->cols;

	if(inMat->cols < inMat->rows) {
		diags = inMat->rows;
	}

	if(inMat->cols != 1 && inMat->rows != 1) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "To generate a matrix from a diagonal that is not 1xN or Nx1.");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	if (result->cols != diags || result->rows != diags) {
			omxResizeMatrix(result, diags, diags, FALSE);
	}

	for(int j = 0; j < diags; j++) {
		for(int k = 0; k < diags; k++) {
			if(j == k) {
				omxSetMatrixElement(result, j, k, omxVectorElement(inMat, j));
			} else {
				omxSetMatrixElement(result, j, k, 0);
			}
		}
	}
}

void omxElementCosine(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Element Cosine.\n");}

	omxMatrix* inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = cos(data[j]);
	}

}

void omxElementCosh(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Element Hyperbolic Cosine.\n");}

	omxMatrix* inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = cosh(data[j]);
	}

}

void omxElementSine(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Element Sine.\n");}

	omxMatrix* inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = sin(data[j]);
	}

}

void omxElementSinh(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Element Hyperbolic Sine.\n");}

	omxMatrix* inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = sinh(data[j]);
	}

}

void omxElementTangent(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Element Tangent.\n");}

	omxMatrix* inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = tan(data[j]);
	}

}

void omxElementTanh(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Element Hyperbolic Tangent.\n");}

	omxMatrix* inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = tanh(data[j]);
	}

}

void omxElementExponent(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Element Exponent.\n");}

	omxMatrix* inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = exp(data[j]);
	}

}

void omxElementNaturalLog(omxMatrix** matList, int numArgs, omxMatrix* result)
{

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Element Natural Log.\n");}

	omxMatrix* inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = log(data[j]);
	}

}

void omxElementSquareRoot(omxMatrix** matList, int numArgs, omxMatrix* result)
{
	omxMatrix *inMat = matList[0];

	int max = inMat->cols * inMat->rows;

	omxCopyMatrix(result, inMat);

	double* data = result->data;
	for(int j = 0; j < max; j++) {
		data[j] = sqrt(data[j]);
	}
}

void omxMatrixVech(omxMatrix** matList, int numArgs, omxMatrix* result) {
	omxMatrix *inMat = matList[0];

	int size;
	if (inMat->rows > inMat->cols) {
		size = inMat->cols * (2 * inMat->rows - inMat->cols + 1) / 2;
	} else {
		size = inMat->rows * (inMat->rows + 1) / 2;
	}

	/* Consistency check: */
	if(result->rows != size || result->cols != 1) {
		omxResizeMatrix(result, size, 1, FALSE);
	}

	int counter = 0;
	for(int i = 0; i < inMat->cols; i++) {
		for(int j = i; j < inMat->rows; j++) {
			omxSetMatrixElement(result, counter, 0, omxMatrixElement(inMat, j, i));
			counter++;
		}
	}

	if(counter != size) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Internal error in vech().\n");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

}

void omxMatrixVechs(omxMatrix** matList, int numArgs, omxMatrix* result) {
	omxMatrix *inMat = matList[0];

	int size;
	if (inMat->rows > inMat->cols) {
		size = inMat->cols * (2 * inMat->rows - inMat->cols + 1) / 2 - inMat->cols;
	} else {
		size = inMat->rows * (inMat->rows + 1) / 2 - inMat->rows;
	}

	/* Consistency check: */
	if(result->rows != size || result->cols != 1) {
		omxResizeMatrix(result, size, 1, FALSE);
	}

	int counter = 0;
	for(int i = 0; i < inMat->cols; i++) {
		for(int j = i + 1; j < inMat->rows; j++) {
			omxSetMatrixElement(result, counter, 0, omxMatrixElement(inMat, j, i));
			counter++;
		}
	}

	if(counter != size) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Internal error in vechs().\n");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

}

void omxRowVectorize(omxMatrix** matList, int numArgs, omxMatrix* result) {
	if(OMX_DEBUG_ALGEBRA) {
		Rprintf("Row Vectorize %s.\n", result->algebra->name);
	}

	omxMatrix *inMat = matList[0];

	int size = (inMat->rows * inMat->cols);

	/* Consistency Check */
	if(result->rows != size || result->cols != 1)
		omxResizeMatrix(result, size, 1, FALSE);

	if(!inMat->colMajor) {		// Special case: we can just memcpy.
		memcpy(result->data, inMat->data, size*sizeof(double));
	} else {
		int next = 0;
		for(int i = 0; i < inMat->rows; i++) {
			for(int j = 0; j < inMat->cols; j++) {
				omxSetMatrixElement(result, next++, 0, omxMatrixElement(inMat, i, j));
			}
		}
	}
}

void omxColVectorize(omxMatrix** matList, int numArgs, omxMatrix* result) {
	if(OMX_DEBUG_ALGEBRA) {
		 Rprintf("Column Vectorize %s.\n", result->algebra->name);
	}

	omxMatrix *inMat = matList[0];

	int size = (inMat->rows * inMat->cols);

	/* Consistency Check */
	if(result->rows != size || result->cols != 1)
		omxResizeMatrix(result, size, 1, FALSE);
	if(inMat->colMajor) {		// Special case: we can just memcpy.
		memcpy(result->data, inMat->data, size * sizeof(double));
	} else {
		int next = 0;
		for(int i = 0; i < inMat->cols; i++) {
			for(int j = 0; j < inMat->rows; j++) {
				omxSetMatrixElement(result, next++, 0, omxMatrixElement(inMat, j, i));
			}
		}
	}
}


void omxSequenceGenerator(omxMatrix** matList, int numArgs, omxMatrix* result) {

	double start = omxVectorElement(matList[0], 0);
	double stop = omxVectorElement(matList[1], 0);

	if (!R_finite(start)) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Non-finite start value in ':' operator.\n");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	if (!R_finite(stop)) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Non-finite stop value in ':' operator.\n");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	double difference = stop - start;
	if (difference < 0) difference = - difference;

	int size = ((int) difference) + 1;

	/* Consistency check: */
	if(result->rows != size || result->cols != 1) {
		omxResizeMatrix(result, size, 1, FALSE);
	}

	/* Sanity-checking.  This loop can be eliminated */
	for(int i = 0; i < size; i++) {
		omxSetVectorElement(result, i, 0);
	}

	int count = 0;
	if ((stop - start) >= 0) {
		while (start <= stop) {
			omxSetVectorElement(result, count, start);
			start = start + 1.0;
			count++;
		}
	} else {
		while (start >= stop) {
			omxSetVectorElement(result, count, start);
			start = start - 1.0;
			count++;
		}
	}
}

void omxMultivariateNormalIntegration(omxMatrix** matList, int numArgs, omxMatrix* result) {

	omxMatrix* cov = matList[0];
	omxMatrix* means = matList[1];
	omxMatrix* lBoundMat = matList[2];
	omxMatrix* uBoundMat = matList[3];

	/* Conformance checks: */
	if (result->rows != 1 || result->cols != 1) omxResizeMatrix(result, 1, 1, FALSE);

	if (cov->rows != cov->cols) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "covariance is not a square matrix");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
		return;
	}

	if (means->rows > 1 && means->cols > 1) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "means is neither row nor column vector");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
		return;
	}

	if (lBoundMat->rows > 1 && lBoundMat->cols > 1) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "lbound is neither row nor column vector");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
		return;
	}

	if (uBoundMat->rows > 1 && uBoundMat->cols > 1) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "ubound is neither row nor column vector");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
		return;
	}

	int nElements = (cov->cols > 1) ? cov->cols : cov->rows;
	double *lBounds, *uBounds;
	double *weights;
	double *corList;
	lBounds = (double*) Calloc(nElements, double);
	uBounds = (double*) Calloc(nElements, double);
	weights = (double*) Calloc(nElements, double);
	corList = (double*) Calloc(nElements * (nElements + 1) / 2, double);

	omxStandardizeCovMatrix(cov, corList, weights);

	// SADMVN calls Alan Genz's sadmvn.f--see appropriate file for licensing info.
	// TODO: Check with Genz: should we be using sadmvn or sadmvn?
	// Parameters are:
	// 	N 		int			# of vars
	//	Lower	double*		Array of lower bounds
	//	Upper	double*		Array of upper bounds
	//	Infin	int*		Array of flags: <0 = (-Inf, Inf) 0 = (-Inf, upper] 1 = [lower, Inf), 2 = [lower, upper]
	//	Correl	double*		Array of correlation coeffs: in row-major lower triangular order
	//	MaxPts	int			Maximum # of function values (use 1000*N or 1000*N*N)
	//	Abseps	double		Absolute error tolerance.  Yick.
	//	Releps	double		Relative error tolerance.  Use EPSILON.
	//	Error	&double		On return: absolute real error, 99% confidence
	//	Value	&double		On return: evaluated value
	//	Inform	&int		On return: 0 = OK; 1 = Rerun, increase MaxPts; 2 = Bad input
	// TODO: Separate block diagonal covariance matrices into pieces for integration separately
	double Error;
	double absEps = 1e-3;
	double relEps = 0;
	int MaxPts = OMX_DEFAULT_MAX_PTS;
	double likelihood;
	int inform;
	int numVars = cov->rows;
	int Infin[cov->rows];

	for(int i = 0; i < nElements; i++) {
		lBounds[i] = (omxVectorElement(lBoundMat, i) - omxVectorElement(means, i))/weights[i];
		uBounds[i] = (omxVectorElement(uBoundMat, i) - omxVectorElement(means, i))/weights[i];
		Infin[i] = 2; // Default to both thresholds
		if(uBounds[i] <= lBounds[i]) {
			char *errstr = Calloc(250, char);
			sprintf(errstr, "Thresholds are not strictly increasing: %3.3f >= %3.3f.", lBounds[i], uBounds[i]);
			omxRaiseError(result->currentState, -1, errstr);
			Free(errstr);
		}
		if(!R_finite(lBounds[i]) ) {
			Infin[i] -= 2;	// NA or INF or -INF means no lower threshold.
		} else {

		}
		if(!R_finite(uBounds[i]) ) {
			Infin[i] -= 1; // NA or INF or -INF means no upper threshold.
		}

	}

	F77_CALL(sadmvn)(&numVars, &(lBounds[0]), &(*uBounds), Infin, corList, &MaxPts, &absEps, &relEps, &Error, &likelihood, &inform);

	if(OMX_DEBUG_ALGEBRA) { Rprintf("Output of sadmvn is %f, %f, %d.\n", Error, likelihood, inform); }

	if(inform == 2) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Improper input to sadmvn.");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	Free(corList);
	Free(weights);
	Free(uBounds);
	Free(lBounds);

	omxSetMatrixElement(result, 0, 0, likelihood);

}

void omxAllIntegrationNorms(omxMatrix** matList, int numArgs, omxMatrix* result) {

	if(OMX_DEBUG_ALGEBRA) {
		Rprintf("All-part multivariate normal integration.\n");
	}

	omxMatrix* cov = matList[0];
	omxMatrix* means = matList[1];
	int nCols = cov->cols;
	int i,j,k;

	int totalLevels = 1;
	omxMatrix *thresholdMats[nCols];
	int numThresholds[nCols], matNums[nCols], thresholdCols[nCols];
	int currentThresholds[nCols];
	int currentMat = 0;

	for(i = currentMat; i < nCols;) {							// Map out the structure of levels.
	if(OMX_DEBUG_ALGEBRA) {
		Rprintf("All-part multivariate normal integration: Examining threshold column %d.\n", i);
	}
		thresholdMats[currentMat] = matList[currentMat+2];		// Get the thresholds for this covariance column

		for(j = 0; j < thresholdMats[currentMat]->cols; j++) {	// We walk along the columns of this threshold matrix
			double ubound, lbound = omxMatrixElement(thresholdMats[currentMat], 0, j);
			if(ISNA(lbound)) {
				char *errstr = Calloc(250, char);
				sprintf(errstr, "Invalid lowest threshold for dimension %d of Allint.", j);
				omxRaiseError(result->currentState, -1, errstr);
				Free(errstr);
				return;
			}

			thresholdCols[i] = j;

			for(k = 1; k < thresholdMats[currentMat]->rows; k++) {
				ubound = omxMatrixElement(thresholdMats[currentMat], k, j);
				if(ISNA(ubound)) {
					numThresholds[i] = k-1;
					totalLevels *= numThresholds[i];
					break;
				}

				if(!(ubound > lbound)) {
					char *errstr = Calloc(250, char);
					sprintf(errstr, "Thresholds (%f and %f) are not strictly increasing for dimension %d of Allint.", lbound, ubound, j+1);
					omxRaiseError(result->currentState, -1, errstr);
					Free(errstr);
					return;
				}

				if(!R_finite(ubound)) {					// Infinite bounds must be last.
					numThresholds[i] = k;
					totalLevels *= numThresholds[i];
					break;
				}

				if(k == (thresholdMats[currentMat]->rows -1)) { // In case the highest threshold isn't Infinity
					numThresholds[i] = k;
					totalLevels *= numThresholds[i];
				}
			}
			currentThresholds[i] = 1;
			matNums[i] = currentMat;
			if(++i >= nCols) {							// We have all we need
				break;
			}
		}
		currentMat++;
	}

	/* Conformance checks: */
	if(result->rows != totalLevels || result->cols != 1) omxResizeMatrix(result, totalLevels, 1, FALSE);

	double weights[nCols];
	double corList[nCols * (nCols + 1) / 2];

	omxStandardizeCovMatrix(cov, &(*corList), &(*weights));

	// SADMVN calls Alan Genz's sadmvn.f--see appropriate file for licensing info.
	// TODO: Check with Genz: should we be using sadmvn or sadmvn?
	// Parameters are:
	// 	N 		int			# of vars
	//	Lower	double*		Array of lower bounds
	//	Upper	double*		Array of upper bounds
	//	Infin	int*		Array of flags: <0 = (-Inf, Inf) 0 = (-Inf, upper] 1 = [lower, Inf), 2 = [lower, upper]
	//	Correl	double*		Array of correlation coeffs: in row-major lower triangular order
	//	MaxPts	int			Maximum # of function values (use 1000*N or 1000*N*N)
	//	Abseps	double		Absolute error tolerance.  Yick.
	//	Releps	double		Relative error tolerance.  Use EPSILON.
	//	Error	&double		On return: absolute real error, 99% confidence
	//	Value	&double		On return: evaluated value
	//	Inform	&int		On return: 0 = OK; 1 = Rerun, increase MaxPts; 2 = Bad input
	// TODO: Separate block diagonal covariance matrices into pieces for integration separately
	double Error;
	double absEps = 1e-3;
	double relEps = 0;
	int MaxPts = OMX_DEFAULT_MAX_PTS;
	double likelihood;
	int inform;
	int numVars = nCols;
	int Infin[nCols];
	double lBounds[nCols];
	double uBounds[nCols];


	/* Set up first row */
	for(j = (nCols-1); j >= 0; j--) {					// For each threshold set, starting from the fastest

		Infin[j] = 2; 									// Default to using both thresholds
		lBounds[j] = (omxMatrixElement(thresholdMats[matNums[j]], currentThresholds[j]-1, thresholdCols[j]) - omxVectorElement(means, j))/weights[j];
		if(!R_finite(lBounds[j])) { 					// Inifinite lower bounds = -Inf to ?
				Infin[j] -= 2;
		}

		uBounds[j] = (omxMatrixElement(thresholdMats[matNums[j]], currentThresholds[j], thresholdCols[j]) - omxVectorElement(means, j))/weights[j];

		if(!R_finite(uBounds[j])) { 					// Inifinite lower bounds = -Inf to ?
				Infin[j] -= 1;
		}

		if(Infin[j] < 0) { Infin[j] = 3; }			// Both bounds infinite.
	}

	F77_CALL(sadmvn)(&numVars, &(lBounds[0]), &(*uBounds), Infin, corList, &MaxPts, &absEps, &relEps, &Error, &likelihood, &inform);

	if(OMX_DEBUG_ALGEBRA) { Rprintf("Output of sadmvn is %f, %f, %d.\n", Error, likelihood, inform); }

	if(inform == 2) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Improper input to sadmvn.");
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	omxSetMatrixElement(result, 0, 0, likelihood);


	/* And repeat with increments for all other rows. */
	for(i = 1; i < totalLevels; i++) {
		for(j = (nCols-1); j >= 0; j--) {							// For each threshold set, starting from the fastest
			currentThresholds[j]++;									// Move to the next threshold set.
			if(currentThresholds[j] > numThresholds[j]) {			// Hit the end; cycle to the next.
				currentThresholds[j] = 1;
			}

			/* Update only the rows that need it. */
			Infin[j] = 2; // Default to both thresholds
			lBounds[j] = (omxMatrixElement(thresholdMats[matNums[j]], currentThresholds[j]-1, thresholdCols[j]) - omxVectorElement(means, j))/weights[j];
			if(!R_finite(lBounds[j])) {								// Inifinite lower bounds = -Inf to ?
				Infin[j] -= 2;
			}
			uBounds[j] = (omxMatrixElement(thresholdMats[matNums[j]], currentThresholds[j], thresholdCols[j]) - omxVectorElement(means, j))/weights[j];

			if(!R_finite(uBounds[j])) { 							// Inifinite lower bounds = -Inf to ?
				Infin[j] -= 1;
			}

			if(Infin[j] < 0) { Infin[j] = 3; }						// Both bounds infinite.

			if(currentThresholds[j] != 1) {							// If we just cycled, we need to see the next set.
				break;
			}

		}

		F77_CALL(sadmvn)(&numVars, &(lBounds[0]), &(*uBounds), Infin, corList, &MaxPts, &absEps, &relEps, &Error, &likelihood, &inform);

		if(OMX_DEBUG_ALGEBRA) { Rprintf("Output of sadmvn is %f, %f, %d.\n", Error, likelihood, inform); }

		if(inform == 2) {
			char *errstr = Calloc(250, char);
			sprintf(errstr, "Improper input to sadmvn.");
			omxRaiseError(result->currentState, -1, errstr);
			Free(errstr);
		}

		omxSetMatrixElement(result, i, 0, likelihood);
	}

}

int omxCompareDoubleHelper(const void* one, const void* two) {
	double diff = *(double*) two - *(double*) one;
	if(diff > EPSILON) {
		return 1;
	} else if(diff < -EPSILON) {
		return -1;
	} else return 0;
}


int omxComparePointerContentsHelper(const void* one, const void* two) {
	double diff = (*(*(double**) two)) - (*(*(double**) one));
	if(diff > EPSILON) {
		return 1;
	} else if(diff < -EPSILON) {
		return -1;
	} else return 0;
}

void omxSortHelper(double* sortOrder, omxMatrix* original, omxMatrix* result) {
	/* Sorts the columns of a matrix or the rows of a column vector
					in decreasing order of the elements of sortOrder. */

	if(OMX_DEBUG) {Rprintf("SortHelper:Original is (%d x %d), result is (%d x %d).\n", original->rows, original->cols, result->rows, result->cols);}

	if(!result->colMajor || !original->colMajor
		|| result->cols != original->cols || result->rows != original->rows) {
		char *errstr = Calloc(250, char);
		sprintf(errstr, "Incorrect input to omxRowSortHelper: %d %d %d %d", result->cols, original->cols, result->rows, original->rows);
		omxRaiseError(result->currentState, -1, errstr);
		Free(errstr);
	}

	double* sortArray[original->rows];
	int numElements = original->cols;
	int numRows = original->rows;

	if(numElements == 1)  numElements = numRows;		// Special case for column vectors

	for(int i = 0; i < numElements; i++) {
		sortArray[i] = sortOrder + i;
	}

	freebsd_mergesort(sortArray, numElements, sizeof(double*), omxComparePointerContentsHelper);

	if(OMX_DEBUG) {Rprintf("Original is (%d x %d), result is (%d x %d).\n", original->rows, original->cols, result->rows, result->cols);}


	for(int i = 0; i < numElements; i++) {
		if(original->cols == 1) {
			omxSetMatrixElement(result, i, 0, omxMatrixElement(original, (sortArray[i] - sortOrder), 0));
		} else {
			memcpy(omxLocationOfMatrixElement(result, 0, i), omxLocationOfMatrixElement(original, 0, sortArray[i]-sortOrder), numRows * sizeof(double));
		}
	}

	return;
}

void omxRealEigenvalues(omxMatrix** matList, int numArgs, omxMatrix* result) {


	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Eigenvalues.\n");}

	omxMatrix* A = omxInitMatrix(NULL, 0, 0, TRUE, result->currentState);
	omxMatrix* B = omxInitMatrix(NULL, 0, 0, TRUE, result->currentState);
	omxCopyMatrix(B, matList[0]);				// FIXME: Much memory badness here.
	omxResizeMatrix(A, B->rows, 1, FALSE);

	/* Conformability Check! */
	if(B->cols != B->rows) {
		char *errstr = calloc(250, sizeof(char));
		sprintf(errstr, "Non-square matrix in eigenvalue decomposition.\n");
		omxRaiseError(B->currentState, -1, errstr);
		free(errstr);
	}

	if(result->rows != B->rows || result->cols != 1)
		omxResizeMatrix(result, B->rows, 1, FALSE);

	char N = 'N';						// Indicators for BLAS
	// char V = 'V';						// Indicators for BLAS

	int One = 1;
	int lwork = 10*B->rows;

	double WI[B->cols];					// Unreported parts
	double work[lwork];					// Storage space
	int info;

	/* For debugging */
	if(OMX_DEBUG_ALGEBRA) {
		omxPrint(result, "NewMatrix");
		Rprintf("DGEEV: %c, %c, %d, %x, %d, %x, %x, %x, %d, %x, %d, %x, %d, %d\n", N, N, (result->rows), result->data, (result->leading), A->data, WI, NULL, One, NULL, One, work, &lwork, &info);
	}

	/* The call itself */
	F77_CALL(dgeev)(&N, &N, &(B->rows), B->data, &(B->leading), A->data, WI, NULL, &One, NULL, &One, work, &lwork, &info);
	if(info != 0) {
		char *errstr = calloc(250, sizeof(char));
		sprintf(errstr, "DGEEV returned %d in (real) eigenvalue decomposition:", info);
		if(info > 0)
			sprintf(errstr, "%s argument %d had an illegal value.  Post this to the OpenMx wiki.\n", errstr, info);
		else
			sprintf(errstr, "%s Unable to decompose matrix: Not of full rank.\n", errstr);
		omxRaiseError(result->currentState, -1, errstr);
		free(errstr);
	}

	result->colMajor = TRUE;

	// Calculate Eigenvalue modulus.
	for(int i = 0; i < A->rows; i++) {
		double value = omxMatrixElement(A, i, 0);
		if(WI[i] != 0) {				// FIXME: Might need to be abs(WI[i] > EPSILON)
			value = sqrt(WI[i]*WI[i] + value*value);				// Sort by eigenvalue modulus
			WI[i] = value;
			WI[++i] = value;										// Conjugate pair.
		} else {
			WI[i] = fabs(value); 									// Modulus of a real is its absolute value
		}
	}

	omxSortHelper(WI, A, result);

	omxFreeMatrixData(A);			// FIXME: Memory Badness here
	omxFreeMatrixData(B);			// FIXME: Memory Badness here
	omxMatrixCompute(result);

}

void omxRealEigenvectors(omxMatrix** matList, int numArgs, omxMatrix* result) {
	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Eigenvalues.\n");}

	omxMatrix* A = omxInitMatrix(NULL, 0, 0, TRUE, result->currentState);
	omxCopyMatrix(result, matList[0]);				// FIXME: Much memory badness here.
	omxResizeMatrix(A, result->rows, result->cols, FALSE);


	if(A == NULL) {
		char *errstr = calloc(250, sizeof(char));
		sprintf(errstr, "Null matrix pointer detected.\n");
		free(errstr);
	}

	/* Conformability Check! */
	if(A->cols != A->rows) {
		char *errstr = calloc(250, sizeof(char));
		sprintf(errstr, "Non-square matrix in (real) eigenvalue decomposition.\n");
		omxRaiseError(result->currentState, -1, errstr);
		free(errstr);
	}

	char N = 'N';						// Indicators for BLAS
	char V = 'V';						// Indicators for BLAS

	int One = 1;
	int lwork = 10*A->rows;

	double WR[A->cols];					// Unreported parts
	double WI[A->cols];					// Unreported parts
	// double VR[A->rows * A->cols];		// Unreported parts
	double work[lwork];					// Storage space
	int info;


	/* For debugging */
	if(OMX_DEBUG_ALGEBRA) {
		omxPrint(result, "NewMatrix");
		Rprintf("DGEEV: %c, %c, %d, %x, %d, %x, %x, %x, %x, %d, %x, %d, %d, %d\n", N, V, (result->rows), result->data, (result->leading), WR, WI, NULL, A->data, &(A->leading), work, lwork, info);
	}

	/* The call itself */
	F77_CALL(dgeev)(&N, &V, &(result->rows), result->data, &(result->leading), WR, WI, NULL, &One, A->data, &(A->leading), work, &lwork, &info);
	if(info != 0) {
		char *errstr = calloc(250, sizeof(char));
		sprintf(errstr, "DGEEV returned %d in eigenvalue decomposition:", info);
		if(info > 0)
			sprintf(errstr, "%s argument %d had an illegal value.  Post this to the OpenMx wiki.\n", errstr, info);
		else
			sprintf(errstr, "%s Unable to decompose matrix: Not of full rank.\n", errstr);
		omxRaiseError(result->currentState, -1, errstr);
		free(errstr);
	}

	// Filter real and imaginary eigenvectors.  Real ones have no WI.
	for(int i = 0; i < A->cols; i++) {
		if(fabs(WI[i]) > EPSILON) {									// If this is part of a conjugate pair
			memcpy(omxLocationOfMatrixElement(A, 0, i+1), omxLocationOfMatrixElement(A, 0, i), A->rows * sizeof(double));
				// ^^ This is column-major, so we can clobber columns over one another.
			WR[i] = sqrt(WR[i] *WR[i] + WI[i]*WI[i]);				// Sort by eigenvalue modulus
			WR[i+1] = WR[i];										// Identical--conjugate pair
			i++; 	// Skip the next one; we know it's the conjugate pair.
		} else {
			WR[i] = fabs(WR[i]); 									// Modulus of a real is its absolute value
		}
	}

	result->colMajor = TRUE;

	// Sort results
	omxSortHelper(WR, A, result);

	omxFreeMatrixData(A);			// FIXME: Potential Memory Badness here
	omxMatrixCompute(result);

}

void omxImaginaryEigenvalues(omxMatrix** matList, int numArgs, omxMatrix* result) {

	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Eigenvalues.\n");}

	omxMatrix* A = omxInitMatrix(NULL, 0, 0, TRUE, result->currentState);
	omxMatrix* B = omxInitMatrix(NULL, 0, 0, TRUE, result->currentState);
	omxCopyMatrix(B, matList[0]);				// FIXME: Potential memory badness here.
	omxResizeMatrix(A, B->rows, 1, FALSE);

	/* Conformability Check! */
	if(B->cols != B->rows) {
		char *errstr = calloc(250, sizeof(char));
		sprintf(errstr, "Non-square matrix in eigenvalue decomposition.\n");
		omxRaiseError(result->currentState, -1, errstr);
		free(errstr);
	}

	if(result->cols != 1 || result->rows != A->rows)
		omxResizeMatrix(result, B->rows, 1, FALSE);

	char N = 'N';						// Indicators for BLAS
	char V = 'V';						// Indicators for BLAS

	int One = 1;
	int lwork = 10*B->rows;

	double WR[B->cols];					// Unreported parts
	double VR[B->rows * B->cols];		// Unreported parts
	double work[lwork];					// Storage space
	int info;

	/* For debugging */
	if(OMX_DEBUG_ALGEBRA) {
		omxPrint(result, "NewMatrix");
		Rprintf("DGEEV: %c, %c, %d, %x, %d, %x, %x, %x, %d, %x, %d, %x, %d, %d\n", N, V, B->data, (B->rows), A->data, WR, NULL, One, VR, (A->rows), work, lwork, info);
	}

	/* The call itself */
	F77_CALL(dgeev)(&N, &N, &(B->rows), B->data, &(B->leading), WR, A->data, NULL, &One, NULL, &One, work, &lwork, &info);
	if(info != 0) {
		char *errstr = calloc(250, sizeof(char));
		sprintf(errstr, "DGEEV returned %d in (real) eigenvalue decomposition:", info);
		if(info > 0)
			sprintf(errstr, "%s argument %d had an illegal value.  Post this to the OpenMx wiki.\n", errstr, info);
		else
			sprintf(errstr, "%s Unable to decompose matrix: Not of full rank.\n", errstr);
		omxRaiseError(result->currentState, -1, errstr);
		free(errstr);
	}

	// Calculate Eigenvalue modulus.
	for(int i = 0; i < result->rows; i++) {
		double value = omxMatrixElement(A, i, 0);					// A[i] is the ith imaginary eigenvalue
		value *= value;												// Squared imaginary part
		if(value > EPSILON) {
			value = sqrt(WR[i] *WR[i] + value);				// Sort by eigenvalue modulus
			WR[i] = value;
			WR[++i] = value;										// Conjugate pair.
		} else {
			WR[i] = fabs(WR[i]);
		}
	}

	result->colMajor = TRUE;

	// Sort results
	omxSortHelper(WR, A, result);

	omxFreeMatrixData(A);				//FIXME: Potential Memory badness here.
	omxMatrixCompute(result);

}

void omxImaginaryEigenvectors(omxMatrix** matList, int numArgs, omxMatrix* result) {
	if(OMX_DEBUG_ALGEBRA) { Rprintf("ALGEBRA: Matrix Eigenvalues.\n");}

	omxMatrix* A = omxInitMatrix(NULL, 0, 0, TRUE, result->currentState);
	omxCopyMatrix(result, matList[0]);				// FIXME: Potential Much memory badness here.
	omxResizeMatrix(A, result->rows, result->cols, FALSE);

	/* Conformability Check! */
	if(A->cols != A->rows) {
		char *errstr = calloc(250, sizeof(char));
		sprintf(errstr, "Non-square matrix in (imaginary) eigenvalue decomposition.\n");
		omxRaiseError(result->currentState, -1, errstr);
		free(errstr);
	}

	char N = 'N';						// Indicators for BLAS
	char V = 'V';						// Indicators for BLAS

	int One = 1;
	int lwork = 10*A->rows;

	double WR[A->cols];					// Unreported parts
	double WI[A->cols];					// Unreported parts
	double work[lwork];					// Storage space
	int info;

	if(result->rows != A->rows || result->cols != A->cols)
		omxResizeMatrix(result, A->rows, A->cols, FALSE);

	/* For debugging */
	if(OMX_DEBUG_ALGEBRA) {
		omxPrint(result, "NewMatrix");
		Rprintf("DGEEV: %c, %c, %d, %x, %d, %x, %x, %x, %d, %x, %d, %x, %d, %d\n", N, V, (A->rows), A->data, (A->leading), WR, WI, NULL, One, result->data, (result->leading), work, lwork, info);
	}

	/* The call itself */
	F77_CALL(dgeev)(&N, &V, &(result->rows), result->data, &(result->leading), WR, WI, NULL, &One, A->data, &(A->leading), work, &lwork, &info);
	if(info != 0) {
		char *errstr = calloc(250, sizeof(char));
		sprintf(errstr, "DGEEV returned %d in eigenvalue decomposition:", info);
		if(info > 0)
			sprintf(errstr, "%s argument %d had an illegal value.  Post this to the OpenMx wiki.\n", errstr, info);
		else
			sprintf(errstr, "%s Unable to decompose matrix: Not of full rank.\n", errstr);
		omxRaiseError(result->currentState, -1, errstr);
		free(errstr);
	}

	// Filter real and imaginary eigenvectors.  Imaginary ones have a WI.
	for(int i = 0; i < result->cols; i++) {
		if(WI[i] != 0) {				// FIXME: Might need to be abs(WI[i] > EPSILON)
			// memcpy(omxLocationOfMatrixElement(A, 0, i), omxLocationOfMatrixElement(A, 0, i+1), A->rows * sizeof(double));
			for(int j = 0; j < result->rows; j++) {
				double value = omxMatrixElement(A, j, i+1);			// Conjugate pair
				omxSetMatrixElement(A, j, i, value);				// Positive first,
				omxSetMatrixElement(A, j, i+1, -value);				// Negative second
			}
			WR[i] = sqrt(WR[i] *WR[i] + WI[i]*WI[i]);				// Sort by eigenvalue modulus
			WR[i+1] = WR[i];										// Identical--conjugate pair
			i++; 	// Skip the next one; we know it's the conjugate pair.
		} else {						// If it's not imaginary, it's zero.
			for(int j = 0; j < A->rows; j++) {
				omxSetMatrixElement(A, j, i, 0.0);
			}
			WR[i] = fabs(WR[i]); 									// Modulus of a real is its absolute value

		}
	}

	result->colMajor = TRUE;

	omxSortHelper(WR, A, result);

	omxFreeMatrixData(A);			// FIXME: Potential Memory Badness here
	omxMatrixCompute(result);

}
