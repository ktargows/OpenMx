library(OpenMx)

set.seed(1)

start_prob <- c(.2,.3,.4)

state <- sample.int(3, 1, prob=start_prob)
trail <- c(state)
for (rep in 1:1000) {
	state <- sample.int(3, 1, prob=start_prob)
	trail <- c(trail, state)
}

noise <- .2
trailN <- sapply(trail, function(v) rnorm(1, mean=v, sd=sqrt(noise)))

classes <- list()

for (cl in 1:3) {
	classes[[cl]] <- mxModel(paste0("class", cl), type="RAM",
			       manifestVars=c("ob"),
			       mxPath("one", "ob", value=cl, free=FALSE),
			       mxPath("ob", arrows=2, value=noise, free=FALSE),
			       mxFitFunctionML(vector=TRUE))
}

mix1 <- mxModel(
	"mix1", classes,
	mxData(data.frame(ob=trailN), "raw"),
	mxMatrix(values=1, nrow=1, ncol=3, free=c(FALSE,TRUE,TRUE), name="weights"),
	mxExpectationMixture(paste0("class",1:3), scale="softmax"),
	mxFitFunctionML(),
	mxComputeSequence(list(
		mxComputeGradientDescent(),
		mxComputeReportExpectation())))

mix1Fit <- mxRun(mix1)

omxCheckEquals(round(mix1Fit$expectation$output$weights,1), c(0,1,0))

mix2 <- mxModel(
  "mix2", classes,
  mxData(data.frame(ob=trailN), "raw"),
  mxMatrix(values=1, nrow=1, ncol=3, free=c(FALSE,TRUE,TRUE), name="initial"),
  mxExpectationHiddenMarkov(paste0("class",1:3), scale="softmax"),
  mxFitFunctionML(),
  mxComputeSequence(list(
    mxComputeGradientDescent(),
    mxComputeReportExpectation())))

mix2Fit <- mxRun(mix2)
omxCheckCloseEnough(mix2Fit$output$fit, mix1Fit$output$fit, 1e-6)
