%
%   Copyright 2007-2017 The OpenMx Project
%
%   Licensed under the Apache License, Version 2.0 (the "License");
%   you may not use this file except in compliance with the License.
%   You may obtain a copy of the License at
% 
%        http://www.apache.org/licenses/LICENSE-2.0
% 
%   Unless required by applicable law or agreed to in writing, software
%   distributed under the License is distributed on an "AS IS" BASIS,
%   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
%   See the License for the specific language governing permissions and
%   limitations under the License.

\name{omxCheckIdentical}
\alias{omxCheckIdentical}

\title{Exact Equality Testing Function}

\description{
   This function tests whether two objects are equal.
}

\usage{
omxCheckIdentical(a, b)
}

\arguments{
   \item{a}{the first value to compare.}
   \item{b}{the second value to compare.}
}

\details{
Performs the \sQuote{identical} comparison on the two arguments. If the two arguments are not equal, then an error will be thrown. If \sQuote{a} and \sQuote{b} are equal to each other, by default the function will print a statement informing the user the test has passed.  To turn off these print statements use \code{options("mxPrintUnitTests" = FALSE)}.
}

\references{
The OpenMx User's guide can be found at http://openmx.ssri.psu.edu/documentation.
}

\seealso{
\code{\link{omxCheckCloseEnough}}, \code{\link{omxCheckWithinPercentError}}, \code{\link{omxCheckSetEquals}}, \code{\link{omxCheckTrue}}, \code{\link{omxCheckEquals}}
}

\examples{

omxCheckIdentical(c(1, 2, 3), c(1, 2, 3))

omxCheckIdentical(FALSE, FALSE)

# Throws an error
try(omxCheckIdentical(c(1, 2, 3), c(2, 1, 3)))

}
