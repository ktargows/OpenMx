#
#   Copyright 2007-2017 The OpenMx Project
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
# 
#        http://www.apache.org/licenses/LICENSE-2.0
# 
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.

# -----------------------------------------------------------------------------
# Program: BivariateSaturated_MatrixCov.R  
# Author: Hermine Maes
# Date: 2009.08.01 
#
# ModelType: Saturated
# DataType: Continuous
# Field: None
#
# Purpose: 
#      Bivariate Saturated model to estimate means and (co)variances
#      Matrix style model input - Covariance matrix data input
#
# RevisionHistory:
#      Hermine Maes -- 2009.10.08 updated & reformatted
#      Ross Gore -- 2011.06.15 added Model, Data & Field metadata
# -----------------------------------------------------------------------------

require(OpenMx)
require(MASS)
# Load Libraries
# -----------------------------------------------------------------------------



set.seed(200)
rs=.5
xy <- mvtnorm::rmvnorm (1000, c(0,0), matrix(c(1,rs,rs,1),2,2))
testData <- xy
testData <- testData[, order(apply(testData, 2, var))[2:1]] #put the data columns in order from largest to smallest variance
# Note: Users do NOT have to re-order their data columns.  This is only to make data generation the same on different operating systems: to fix an inconsistency with the mvtnorm::rmvnorm function in the MASS package.
selVars <- c("X","Y")
dimnames(testData) <- list(NULL, selVars)
summary(testData)
covData <- cov(testData)
# Simulate Data
# -----------------------------------------------------------------------------


bivSatModel3 <- mxModel("bivSat3",
    mxMatrix(
        type="Symm", 
        nrow=2, 
        ncol=2, 
        free=T, 
        values=c(1,.5,1), 
        name="expCov"
    ),
    mxData(
        observed=covData, 
        type="cov", 
        numObs=1000 
    ),
    mxFitFunctionML(),mxExpectationNormal(
        covariance="expCov",
        dimnames=selVars
    )
)

bivSatFit3 <- mxRun(bivSatModel3)
EC3 <- mxEval(expCov, bivSatFit3)
LL3 <- mxEval(objective,bivSatFit3)
bivSatSummary3 <- summary(bivSatFit3) 
SL3 <- bivSatSummary3$SaturatedLikelihood
omxCheckEquals(bivSatSummary3$observedStatistics, nrow(covData) * (ncol(covData) + 1) / 2)
Chi3 <- LL3-SL3
# example 3: Saturated Model with Cov Matrices and Matrix-Style Input
# -----------------------------------------------------------------------------


bivSatModel3m <- mxModel("bivSat3m",
    mxMatrix(
        type="Symm", 
        nrow=2, 
        ncol=2, 
        free=T, 
        values=c(1,.5,1), 
        name="expCov"
    ),
    mxMatrix(
        type="Full", 
        nrow=1, 
        ncol=2, 
        free=T, 
        values=c(0,0), 
        name="expMean"
    ),
    mxData(
        observed=cov(testData), 
        type="cov", 
        numObs=1000, 
        means=colMeans(testData) 
    ),
    mxFitFunctionML(),mxExpectationNormal(
        covariance="expCov",
        means="expMean",
        dimnames=selVars
    )
)

bivSatFit3m <- mxRun(bivSatModel3m)
EM3m <- mxEval(expMean, bivSatFit3m)
EC3m <- mxEval(expCov, bivSatFit3m)
LL3m <- mxEval(objective,bivSatFit3m)
SL3m <- summary(bivSatFit3m)$SaturatedLikelihood
Chi3m <- LL3m-SL3m
# example 3m: Saturated Model with Cov Matrices & Means and Matrix-Style Input
# -----------------------------------------------------------------------------


omxCheckCloseEnough(Chi3, -0.001, .001)
omxCheckCloseEnough(c(EC3),c(1.065, 0.475, 0.475, 0.929),.001)
# 3:CovMat
# -------------------------------------

omxCheckCloseEnough(Chi3m, -0.001, .001)
omxCheckCloseEnough(c(EC3m),c(1.065, 0.475, 0.475, 0.929),.001)
omxCheckCloseEnough(c(EM3m),c(0.058, 0.006),.001)
# 3m:CovMPat 
# -------------------------------------
# Compare OpenMx results to Mx results 
# (LL: likelihood; EC: expected covariance, EM: expected means)
# -----------------------------------------------------------------------------
