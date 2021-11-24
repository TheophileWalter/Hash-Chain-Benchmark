# C implementation of HCB
## How to build?
```gcc hcb.c sha256.c -o hcb```  
## Usage
```hcb MESSAGE FILE | --continue FILE | --check FILE```  
|Parameter|Description|
|-|-|
|```MESSAGE FILE```|Starts a new hash chain with the given MESSAGE and saves it to FILE|
|```--continue FILE```|Continue a hash chain contained in FILE<br />The new blocks will be automatically added to the FILE|
|```--check FILE```|Check the validity of a hash chain contained in FILE|
