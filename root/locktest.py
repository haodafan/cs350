import os

# Run the fucking lock test 100 times 

os.system("sys161 kernel 'sy2;q' > pytestresult.txt")
os.system("echo '' > pytestsummary.txt") # Clears the file
for i in range(0,100): 
    os.system("sys161 kernel 'sy2;q' > pytestresultcont.txt")
    os.system("diff pytestresult.txt pytestresultcont.txt >> pytestsummary.txt")
