Fast version of `du`. Right now it's about 30% faster, planning to introduce
threading soon which should make it substantially better. Also potentially
caching or the like in one of the attributes people rarely use.

I lost the updated source code to this when i left my job. The short version of my findings is that the majority of the time was spent in very large directories (>10k files). This was ameliorated by using getdirentries64(). The underlying reason for this appeared to have been that getattrlistbulk() generated 5 times more RdMeta requests to the SSD than getdirentries64(), even when getattrlistbulk() gets the same attributes. I only tested this on APFS, not HFS+. I lacked time to figure out why this was as I am not a filesystems expert and the APFS team was working on other things.
