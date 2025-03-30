import System.Process (callCommand)
import System.Directory (setCurrentDirectory)

main :: IO ()
main = do
    putStrLn "Cambiando a carpeta build/..."
    setCurrentDirectory "build"

    putStrLn "Compilando con make..."
    callCommand "make TestServer"

    putStrLn "Ejecutando Tests..."
    callCommand "./TestServer"