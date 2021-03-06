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


#-------------------------------------------------------------------------------------
# Author: Michael D. Hunter
# Date: 2012.10.23
# Filename: mxSaturatedModel.R
# Purpose: This is a helper function for making and fitting a saturated model.
#  It takes an mxModel and gives back the fitted saturated model.
#  Example usage
#    amod <- mxModel(blah)
#    smod <- mxSaturatedModel(amod)
#    summary(amod, SaturatedLikelihood=smod)
#-------------------------------------------------------------------------------------


#-------------------------------------------------------------------------------------
# Revision History
# Tue Oct 23 00:55:48 Central Daylight Time 2012 -- Michael Hunter copied file from personal work
# Wed 19 Jun 2013 12:23:45 Central Daylight Time -- Michael Hunter added ordinal support etc with aid of Mike Neale and Ryne Estabrook
# 


#-------------------------------------------------------------------------------------
# 1. Add ability to fit multiple groups
# 2. If given a model whose data set has 10 variables but the model only uses 2
#  variables, adjust function to make a saturated model of only the 2 used variables.
# TODO Check that the above are done reasonably correct.
# TODO Improve interaction between 1 & 2.
# Added ability to do Independence model.
# Added Fix variances of binary variables to 1.0
#  via mxConstraint on filtered expected cov.


#-------------------------------------------------------------------------------------
# Saturated Model function definition

generateNormalReferenceModels <- function(modelName, obsdata, datatype, withMeans=FALSE, numObs, means=NA) {
	datasource <- mxData(observed=obsdata, type=datatype, numObs=numObs, means=means)
	numVar <- ncol(obsdata)
	varnam <- colnames(obsdata)
	if(is.null(varnam)) {
		varnam <- paste("V", 1:numVar, sep="")
		dimnames(obsdata) <- list(varnam, varnam)
	}
	if(datatype == "raw") {
		if (is.data.frame(obsdata)) {
			ordinalCols <- sapply(obsdata, is.ordered)
		} else {
			ordinalCols <- rep(FALSE, numVar)
		}
		if(!any(ordinalCols)){
			sampcov <- cov(obsdata, use="pairwise.complete.obs")
			startcov <- try(t(chol(sampcov)))
			# if the cholesky fails, just use the diagonal elements
			if(class(startcov) %in% "try-error"){
				startcov <- t(chol(diag(diag(sampcov), nrow=nrow(sampcov))))
			}
			startcov <- startcov[lower.tri(startcov, TRUE)]
			indepcov <- diag(sampcov)
			startmea <- colMeans(obsdata, na.rm=TRUE)
		}
		else {
			ordnam <- names(obsdata[,ordinalCols])
			ordinalLevels <- lapply(obsdata[,ordinalCols], levels)
			numOrdinal <- sum(ordinalCols)
			numOrdinalLevels <- sapply(ordinalLevels, length)
			maxLevels <- max(numOrdinalLevels)
			numThresholds <- maxLevels-1
			startcov <- t(chol(diag(1, numVar)))
			startcov <- startcov[lower.tri(startcov, TRUE)]
			indepcov <- diag(1, numVar)
			startmea <- rep(0, numVar)
		}
	} else {
		startcov <- 0.3
		indepcov <- 0.3
		startmea <- 3.0
		ordinalCols <- rep(FALSE, numVar)
	}

	# For all continuous data, use the Cholesky decomposition
	#  but for joint and all-ordinal use a symmetric matrix
	# This allows one to "contrain" the total variance of ordinal variables
	#  to 1 without using mxConstraint.
	if(!any(ordinalCols)){
		ltCov <- mxMatrix(type="Lower", nrow=numVar, ncol=numVar,
				values=startcov, free=TRUE, name="ltCov")
		diag(ltCov$lbound) <- 0
		satCov <- mxAlgebra(name="satCov", expression= ltCov %*% t(ltCov), dimnames=list(varnam, varnam))
	} else {
		ltCov <- NULL
		satFre <- matrix(as.logical(diag(!ordinalCols, numVar)), numVar, numVar)
		satFre[lower.tri(satFre, diag=FALSE)] <- TRUE
		satCov <- mxMatrix(type="Symm", nrow=numVar, ncol=numVar,
				values=startcov, free=satFre[lower.tri(satFre, diag=TRUE)], name="satCov",
				dimnames=list(varnam, varnam))
	}
	saturatedModel <- mxModel(name=paste("Saturated", modelName),
					datasource,
					ltCov,
					satCov,
					mxExpectationNormal("satCov"),
					mxFitFunctionML())

	indCov <- mxMatrix(type="Diag", nrow=numVar, ncol=numVar, values=indepcov, free=!ordinalCols,
				lbound=0, name="indCov", dimnames=list(varnam, varnam))
	independenceModel <- mxModel(name=paste("Independence", modelName),
				     datasource, indCov,
				     mxExpectationNormal("indCov"), mxFitFunctionML())

	if(datatype == "raw" || withMeans) {
		saturatedModel <- mxModel(saturatedModel,
			mxMatrix(nrow=1, ncol=numVar, values=startmea, free=TRUE, name="satMea", dimnames=list(NA, varnam)),
			mxExpectationNormal("satCov", "satMea")
		)
		independenceModel <- mxModel(independenceModel,
			mxMatrix(nrow=1, ncol=numVar, values=startmea, free=TRUE, name="satMea", dimnames=list(NA, varnam)),
			mxExpectationNormal("indCov", "satMea")
		)
		if(any(ordinalCols)) {
			thrdnam <- paste(rep(ordnam, each=numThresholds), 'ThrDev', 1:numThresholds, sep='')
			unitLower <- mxMatrix("Lower", numThresholds, numThresholds, values=1, free=FALSE, name="unitLower")
			thrdM <- rbind(numOrdinalLevels-1, numThresholds - numOrdinalLevels+1)
			thrdfre <- apply(thrdM, 2, rep, x=c(TRUE, FALSE))
			thresholdDeviations <- mxMatrix("Full", 
					name="thresholdDeviations", nrow=numThresholds, ncol=numOrdinal,
					values=.2,
					free = thrdfre,
					labels=thrdnam,
					lbound = rep( c(-Inf,rep(.01, (numThresholds-1))) , numOrdinal), # TODO adjust increment value
					dimnames = list(c(), varnam[ordinalCols]),
							)
			saturatedMeans <- mxMatrix(nrow=1, ncol=numVar,
				values=startmea, free=c(!ordinalCols), name="satMea", dimnames=list(NA, varnam))
			saturatedThresholds <- mxAlgebra(unitLower %*% thresholdDeviations, name="thresholdMatrix")
			saturatedModel <- mxModel(saturatedModel,
				saturatedMeans, thresholdDeviations, unitLower, saturatedThresholds,
				mxExpectationNormal("satCov", "satMea", thresholds="thresholdMatrix")
			)
			independenceModel <- mxModel(independenceModel,
				saturatedMeans, thresholdDeviations, unitLower, saturatedThresholds,
				mxExpectationNormal("indCov", "satMea", thresholds="thresholdMatrix")
			)
		}
	}
	return(list(Saturated=saturatedModel, Independence=independenceModel))
}

generateIFAReferenceModels <- function(model) {
	modelName <- model@name
	expectation <- model@expectation

	spec <- expectation$ItemSpec
	nullspec <- lapply(spec, rpf::rpf.modify, 0)
	data <- model$data$observed
	itemName <- expectation$item
	item <- model[[itemName]]
	nullitem <- mxMatrix(name="item", values=mxSimplify2Array(lapply(nullspec, rpf::rpf.rparam)), condenseSlots=FALSE)

	if (is.null(item)) {
		stop(paste("Cannot find matrix", omxQuotes(itemName),"in model",
			   omxQuotes(modelName),"to create independence model"))
	}

	pmap <- matrix(NA, nrow(nullitem), ncol(nullitem))
	for (cx in 1:ncol(item)) {
		map1 <- match(names(rpf::rpf.rparam(nullspec[[cx]])),
			      names(rpf::rpf.rparam(spec[[cx]])))
		if (!length(map1)) next
		pmap[1:length(map1),cx] <- item$labels[map1,cx]
	}
	nullitem$labels[,] <- pmap

	ind <- mxModel(name=paste("Independence", modelName),
		       nullitem, model$data,
		       mxExpectationBA81(ItemSpec=nullspec,
					 qpoints = expectation$qpoints,
					 qwidth = expectation$qwidth),
		       mxFitFunctionML(),
		       # Only need 1 iteration, but allow 2 to avoid code BLUE warning.
		       mxComputeEM(estep=mxComputeOnce('expectation', 'scores'),
				   mstep=mxComputeSequence(list(
					   mxComputeNewtonRaphson(),
					   mxComputeOnce('expectation'))),
				   maxIter = 2L))
	dimnames(ind$item) = list(paste('p', 1:nrow(ind$item), sep=""), colnames(item))
	ind$item$free <- !is.na(ind$item$values)

	weightColumn <- expectation$weightColumn
	if (!is.na(weightColumn)) {
		weights <- data[weightColumn]
	} else if (!is.na(model$data$weight)) {
		weights <- data[model$data$weight]
	} else {
		data <- data[rpf::orderCompletely(data),]
		weights <- as.numeric(rpf::tabulateRows(data))
	}
	saturated <- NA
	if (!any(is.na(data[1,]))) {  # Not sure how to handle missingness
		saturated <- -2 * sum(weights * log(weights / sum(weights)))
	}

	return(list(Saturated=list(fit=saturated, df=0),
		    Independence=ind))
}

ReferenceModelHelper <- function(x) {
	if ( (!(isS4(x) && is(x, "MxModel"))) && !is.data.frame(x) && !(is.matrix(x) && is.numeric(x)) ) {
		stop("The 'x' argument must be (1) an MxModel object, (2) a raw data frame, or (3) a raw data matrix.")
	}
	if ( is(x, "MxModel") ) {
		if (is.null(x$fitfunction)) {
			stop("Model", omxQuotes(x$name), "has no fitfunction")
		}
		generateReferenceModels(x$fitfunction, x)
	} else {
		obsdata <- x
		if(ncol(obsdata) != nrow(obsdata)) {
			datatype <- "raw"
		}
		else {datatype <- "cov"}
		generateNormalReferenceModels("Data Model", obsdata, datatype)
	}
}

mxRefModels <- function(x, run=FALSE) {
	models <- lapply(ReferenceModelHelper(x), function(model) {
		if (!isS4(model)) return(model)
		model <- omxAssignFirstParameters(model)
		model <- mxOption(model, "Standard Errors", "No")
		model <- mxOption(model, "Calculate Hessian", "No")
		if (run) {
			model <- mxRun(model, silent=FALSE)
		}
		model
	})
	models
}

omxSaturatedModel <- mxRefModels # old name
